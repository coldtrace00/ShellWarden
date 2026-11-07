// Windows part of privatedir (no Qt here, see PrivateDir.h).
#ifdef _WIN32
#include "PrivateDir.h"

#include <windows.h>
#include <aclapi.h>

#include <memory>
#include <vector>

namespace privatedir {

namespace {

struct LocalFreer { void operator()(void *p) const { if (p) LocalFree(p); } };

std::vector<unsigned char> currentUserSid(unsigned long *err)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        *err = GetLastError();
        return {};
    }
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    std::vector<unsigned char> buf(len);
    const bool ok = len && GetTokenInformation(token, TokenUser, buf.data(), len, &len);
    if (!ok)
        *err = GetLastError();
    CloseHandle(token);
    if (!ok)
        return {};
    const PSID sid = reinterpret_cast<TOKEN_USER *>(buf.data())->User.Sid;
    std::vector<unsigned char> out(GetLengthSid(sid));
    CopySid(DWORD(out.size()), out.data(), sid);
    return out;
}

std::vector<unsigned char> wellKnown(WELL_KNOWN_SID_TYPE type)
{
    DWORD len = SECURITY_MAX_SID_SIZE;
    std::vector<unsigned char> out(len);
    if (!CreateWellKnownSid(type, nullptr, out.data(), &len))
        return {};
    out.resize(len);
    return out;
}

// Already private: protected list whose allow entries name only the three accounts.
bool alreadyPrivate(const wchar_t *path, const std::vector<std::vector<unsigned char>> &allowed)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sdRaw = nullptr;
    if (GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
                              &sdRaw) != ERROR_SUCCESS)
        return false;
    std::unique_ptr<void, LocalFreer> sd(sdRaw);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!dacl || !GetSecurityDescriptorControl(sd.get(), &control, &revision) || !(control & SE_DACL_PROTECTED))
        return false;
    ACL_SIZE_INFORMATION info{};
    if (!GetAclInformation(dacl, &info, sizeof info, AclSizeInformation))
        return false;
    for (DWORD i = 0; i < info.AceCount; ++i) {
        void *ace = nullptr;
        if (!GetAce(dacl, i, &ace))
            return false;
        const auto *header = static_cast<ACE_HEADER *>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
            continue;   // deny entries only take rights away
        PSID sid = &static_cast<ACCESS_ALLOWED_ACE *>(ace)->SidStart;
        bool known = false;
        for (const auto &a : allowed)
            known = known || (!a.empty() && EqualSid(sid, PSID(a.data())));
        if (!known)
            return false;
    }
    return true;
}

} // namespace

int protectWindowsDir(const wchar_t *path, unsigned long *winError)
{
    unsigned long err = 0;
    const std::vector<unsigned char> user = currentUserSid(&err);
    const std::vector<unsigned char> system = wellKnown(WinLocalSystemSid);
    const std::vector<unsigned char> admins = wellKnown(WinBuiltinAdministratorsSid);
    if (user.empty() || system.empty() || admins.empty()) {
        if (winError)
            *winError = err ? err : GetLastError();
        return -1;
    }
    if (alreadyPrivate(path, {user, system, admins}))
        return 0;

    EXPLICIT_ACCESS_W ea[3] = {};
    const std::vector<unsigned char> *sids[3] = {&user, &system, &admins};
    for (int i = 0; i < 3; ++i) {
        ea[i].grfAccessPermissions = GENERIC_ALL;
        ea[i].grfAccessMode = SET_ACCESS;
        ea[i].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea[i].Trustee.TrusteeType = i == 0 ? TRUSTEE_IS_USER : TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea[i].Trustee.ptstrName = reinterpret_cast<LPWSTR>(const_cast<unsigned char *>(sids[i]->data()));
    }
    PACL aclRaw = nullptr;
    DWORD rc = SetEntriesInAclW(3, ea, nullptr, &aclRaw);
    std::unique_ptr<void, LocalFreer> acl(aclRaw);
    if (rc == ERROR_SUCCESS) {
        // Protected = parent permissions (e.g. "Users: modify" under Program Files)
        // no longer apply; the new entries are inherited by everything inside.
        rc = SetNamedSecurityInfoW(const_cast<LPWSTR>(path), SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                   nullptr, nullptr, aclRaw, nullptr);
    }
    if (rc != ERROR_SUCCESS) {
        if (winError)
            *winError = rc;
        return -1;
    }
    return 1;
}

} // namespace privatedir
#endif
