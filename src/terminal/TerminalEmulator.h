#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <deque>
#include <vector>

extern "C" {
#include <vterm.h>
}

// A colour scheme for the terminal.
struct TerminalTheme
{
    QString id;
    QString name;
    QColor fg, bg, cursor, selection;
    QColor ansi[16];
    // user themes only
    bool custom = false;
    QString bgImage;     // local file shown behind the text
    int bgDim = 60;      // 0..100: how much the background colour covers the image
    bool bgFit = false;  // true: whole picture visible (full height, on the right); false: fill and crop
    QColor input;        // colour for the command you type (applied through the shell prompt)
    bool autoInput = false;   // send the prompt command automatically after connecting
};

// Terminal state for one session: libvterm screen + scrollback.
// It is owned by the session (not by a view), so the same session can be
// shown in the main window, a detached window or a split pane without
// losing its content.
class TerminalEmulator : public QObject
{
    Q_OBJECT
public:
    static constexpr uint32_t kWideContinuation = 0xFFFFFFFFu;

    explicit TerminalEmulator(QObject *parent = nullptr);
    ~TerminalEmulator() override;

    // ---- themes -----------------------------------------------------
    static const std::vector<TerminalTheme> &themes();
    static const TerminalTheme &themeById(const QString &id);
    static QVariantList themesForQml();
    // User-made themes (Settings → Terminal → Create theme). Ids start with "custom-".
    static void setCustomThemes(const QVariantList &list);
    // Built-in picture themes: the user may put their own picture behind them.
    // {themeId: {image, dim}}; an empty map restores the pictures that ship with the app.
    static TerminalTheme themeFromMap(const QVariantMap &m);
    // bash line that colours the command you type and resets the colour for its output
    static QString inputColorCommand(const QColor &c);
    void setTheme(const QString &id);
    void reloadTheme();   // re-read after a custom theme was edited
    QString themeId() const { return m_theme.id; }
    const TerminalTheme &theme() const { return m_theme; }

    // ---- data in / out ----------------------------------------------
    void feed(const QByteArray &data);
    void resize(int rows, int cols);
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    // keyboard / mouse / paste → produce bytes via output()
    void key(VTermKey key, VTermModifier mod);
    void unichar(uint32_t c, VTermModifier mod);
    void mouseMove(int row, int col, VTermModifier mod);
    void mouseButton(int button, bool pressed, VTermModifier mod);
    void startPaste();
    void endPaste();

    // ---- screen access ----------------------------------------------
    // absLine: 0 .. scrollbackSize()+rows()-1 (scrollback first, then screen)
    bool cellAt(int absLine, int col, VTermScreenCell *cell) const;
    QColor resolve(const VTermColor &c, bool fg) const;
    int scrollbackSize() const { return int(m_scrollback.size()); }
    void clearScrollback();
    void setScrollbackLimit(int lines);
    VTermPos cursor() const { return m_cursor; }
    bool cursorVisible() const { return m_cursorVisible; }
    bool altScreen() const { return m_altScreen; }
    // Text of the logical line under the cursor (wrapped rows joined), trailing spaces cut.
    QString cursorLineText() const;
    // Same for any line (absLine as in cellAt); full = the last column is used (a wrapped row).
    QString lineText(int absLine, bool *full = nullptr) const;
    int cursorLineStart() const;   // absLine where the cursor's logical line begins
    int mouseMode() const { return m_mouseMode; }
    QString title() const { return m_title; }
    void reset();

signals:
    void output(const QByteArray &bytes);   // to the remote side
    void damaged();
    void scrollbackPushed();                // one line moved into scrollback
    void scrollbackChanged();
    void titleChanged();
    void bell();
    void themeChanged();

private:
    static int cbDamage(VTermRect rect, void *user);
    static int cbMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int cbSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int cbBell(void *user);
    static int cbSbPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int cbSbPopLine(int cols, VTermScreenCell *cells, void *user);
    static int cbSbClear(void *user);
    static void cbOutput(const char *s, size_t len, void *user);

    void applyPalette();

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    int m_rows = 24, m_cols = 80;
    std::deque<std::vector<VTermScreenCell>> m_scrollback;
    int m_scrollbackMax = 10000;
    VTermPos m_cursor{0, 0};
    bool m_cursorVisible = true;
    bool m_altScreen = false;
    bool isContinuation(int row) const;
    int lastTextRow(int rows, int cols) const;
    int m_mouseMode = 0;
    QByteArray m_titleBuf;
    QString m_title;
    TerminalTheme m_theme;
};
