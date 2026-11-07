#include "TerminalItem.h"
#include "PasteGuard.h"
#include "app/TerminalSession.h"

#include <QClipboard>
#include <QCursor>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QImage>
#include <QPainter>
#include <QLinearGradient>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

constexpr uint32_t kWideContinuation = TerminalEmulator::kWideContinuation;

// Marks bytes produced while handling a key press / paste as typed input
// (they go to the broadcast group; terminal query replies do not).
struct InputGuard
{
    QPointer<TerminalSession> s;
    explicit InputGuard(TerminalSession *session) : s(session) { if (s) s->beginUserInput(); }
    ~InputGuard() { if (s) s->endUserInput(); }
};

// Output highlighting rules. Order matters: earlier rules win.
struct HlRule
{
    QRegularExpression re;
    int ansi;       // palette index used as the colour
    bool bold;
};

const std::vector<HlRule> &hlRules()
{
    static const std::vector<HlRule> rules = [] {
        const auto ci = QRegularExpression::CaseInsensitiveOption;
        std::vector<HlRule> r;
        r.push_back({QRegularExpression(QStringLiteral(
                         "\\b(error|errors|failed|failure|fail|fatal|denied|refused|critical|panic|"
                         "unreachable|timed out|timeout|not found|no such file|cannot|can't|unable|invalid|"
                         "segfault|exception|traceback|killed|aborted|emerg|alert|err)\\b"), ci), 9, true});
        r.push_back({QRegularExpression(QStringLiteral("\\b(warning|warnings|warn|deprecated|caution|retry|retrying)\\b"), ci), 11, true});
        r.push_back({QRegularExpression(QStringLiteral(
                         "\\b(ok|okay|success|successful|successfully|succeeded|done|passed|complete|completed|"
                         "enabled|connected|accepted|listening)\\b"), ci), 10, false});
        r.push_back({QRegularExpression(QStringLiteral("\\bactive \\(running\\)"), ci), 10, true});
        r.push_back({QRegularExpression(QStringLiteral("\\b[a-z][a-z0-9+.-]*://[^\\s'\"<>]+"), ci), 12, false});
        r.push_back({QRegularExpression(QStringLiteral(
                         "\\b(?:(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)\\.){3}(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)(?:/\\d{1,2})?(?::\\d{1,5})?\\b")), 14, false});
        // IPv6: either compressed ("::") or all 8 groups, so times and MACs don't match
        r.push_back({QRegularExpression(QStringLiteral(
                         "(?<![\\w:])(?=[0-9a-f:]*::|(?:[0-9a-f]{1,4}:){7}[0-9a-f])[0-9a-f]{0,4}(?::[0-9a-f]{0,4}){2,7}(?![\\w:])"), ci), 14, false});
        r.push_back({QRegularExpression(QStringLiteral("\\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\\b"), ci), 13, false});
        r.push_back({QRegularExpression(QStringLiteral(
                         "\\b\\d{4}-\\d{2}-\\d{2}(?:[ T]\\d{2}:\\d{2}(?::\\d{2})?)?\\b|\\b\\d{2}:\\d{2}:\\d{2}\\b")), 13, false});
        r.push_back({QRegularExpression(QStringLiteral("(?<![\\w/.-])(?:~|\\.{1,2})?/[\\w.@%+-]+(?:/[\\w.@%+-]*)*")), 12, false});
        return r;
    }();
    return rules;
}

QString cellText(const VTermScreenCell &cell)
{
    if (cell.chars[0] == 0 || cell.chars[0] == kWideContinuation)
        return QStringLiteral(" ");
    QString s;
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) {
        const char32_t cp = cell.chars[i];
        s += QString::fromUcs4(&cp, 1);
    }
    return s;
}

bool isWordChar(const VTermScreenCell &cell)
{
    const uint32_t c = cell.chars[0];
    if (c == 0 || c == ' ' || c == kWideContinuation)
        return false;
    if (c > 0x7f)
        return true;
    return QChar(char16_t(c)).isLetterOrNumber() || QStringLiteral("-_.:/~@%+#=?&").contains(QChar(char16_t(c)));
}

} // namespace

QString TerminalItem::defaultFontFamily()
{
    const QStringList families = QFontDatabase::families();
    for (const char *name : {"Cascadia Mono", "Cascadia Code", "JetBrains Mono", "Consolas", "DejaVu Sans Mono", "Liberation Mono"}) {
        if (families.contains(QString::fromLatin1(name)))
            return QString::fromLatin1(name);
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

TerminalItem::TerminalItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setActiveFocusOnTab(false);
    setAntialiasing(false);
    setCursor(Qt::IBeamCursor);

    m_font = QFont(defaultFontFamily(), 11);
    m_font.setStyleHint(QFont::TypeWriter);
    m_font.setFixedPitch(true);
    m_font.setKerning(false);
    m_font.setHintingPreference(QFont::PreferFullHinting);
    m_fontBold = m_font;
    m_fontBold.setBold(true);
    updateMetrics();
    setFillColor(QColor(0x12, 0x14, 0x1c));
}

TerminalItem::~TerminalItem() = default;

QColor TerminalItem::backgroundColor() const
{
    return m_emu ? m_emu->theme().bg : QColor(0x12, 0x14, 0x1c);
}

// ---------------------------------------------------------------------------
// session wiring
// ---------------------------------------------------------------------------

void TerminalItem::setSession(TerminalSession *s)
{
    if (m_session == s)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    if (m_emu)
        disconnect(m_emu, nullptr, this, nullptr);
    m_session = s;
    m_emu = s ? s->emulator() : nullptr;
    m_scrollOffset = 0;
    m_hasSelection = false;
    if (m_emu) {
        m_emu->setScrollbackLimit(m_scrollbackMax);
        connect(m_emu, &TerminalEmulator::damaged, this, [this] { update(); });
        connect(m_emu, &TerminalEmulator::titleChanged, this, &TerminalItem::titleChanged);
        connect(m_emu, &TerminalEmulator::bell, this, &TerminalItem::bell);
        connect(m_emu, &TerminalEmulator::scrollbackChanged, this, [this] {
            m_scrollOffset = std::min(m_scrollOffset, m_emu->scrollbackSize());
            emit scrollChanged();
        });
        connect(m_emu, &TerminalEmulator::scrollbackPushed, this, [this] {
            // keep the viewed lines in place while the user reads history
            if (m_scrollOffset > 0)
                m_scrollOffset = std::min(m_scrollOffset + 1, m_emu->scrollbackSize());
        });
        connect(m_emu, &TerminalEmulator::themeChanged, this, [this] {
            setFillColor(m_emu->theme().bg);
            emit themeChanged();
            update();
        });
        setFillColor(m_emu->theme().bg);
        emit themeChanged();
        resizeGrid(true);
    }
    emit sessionChanged();
    emit titleChanged();
    emit scrollChanged();
    update();
}

void TerminalItem::setHighlightOutput(bool on)
{
    if (on == m_highlight)
        return;
    m_highlight = on;
    emit optionsChanged();
    update();
}

void TerminalItem::setScrollbackLines(int n)
{
    if (n == m_scrollbackMax)
        return;
    m_scrollbackMax = n;
    if (m_emu)
        m_emu->setScrollbackLimit(n);
    emit optionsChanged();
}

void TerminalItem::writeToSession(const QByteArray &data)
{
    if (m_session && !data.isEmpty())
        m_session->userInput(data);
}

void TerminalItem::sendText(const QString &text)
{
    writeToSession(text.toUtf8());
}

// ---------------------------------------------------------------------------
// geometry & font
// ---------------------------------------------------------------------------

void TerminalItem::setFontFamily(const QString &family)
{
    const QString f = family.trimmed().isEmpty() ? defaultFontFamily() : family.trimmed();
    if (f == m_font.family())
        return;
    m_font.setFamily(f);
    m_fontBold.setFamily(f);
    updateMetrics();
    resizeGrid();
    emit fontChanged();
    update();
}

void TerminalItem::setFontSize(int pt)
{
    pt = std::clamp(pt, 6, 40);
    if (pt == m_font.pointSize())
        return;
    m_font.setPointSize(pt);
    m_fontBold.setPointSize(pt);
    updateMetrics();
    resizeGrid();
    emit fontChanged();
    update();
}

void TerminalItem::updateMetrics()
{
    const QFontMetricsF fm(m_font);
    m_cw = fm.horizontalAdvance(QLatin1Char('M'));
    m_ch = std::ceil(fm.height());
    m_ascent = fm.ascent();
    if (m_cw <= 0)
        m_cw = 8;
    if (m_ch <= 0)
        m_ch = 16;
}

void TerminalItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        resizeGrid();
}

void TerminalItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickPaintedItem::itemChange(change, value);
    // The view that becomes visible decides the terminal size.
    if (change == ItemVisibleHasChanged && value.boolValue)
        resizeGrid(true);
}

void TerminalItem::resizeGrid(bool force)
{
    if (width() <= 0 || height() <= 0)
        return;
    const int cols = std::max(10, int(width() / m_cw));
    const int rows = std::max(3, int(height() / m_ch));
    const bool changed = cols != m_cols || rows != m_rows;
    m_cols = cols;
    m_rows = rows;
    if ((changed || force) && m_session && m_resizesSession && isVisible())
        m_session->resizeTerminal(cols, rows);
    if (changed)
        emit gridSizeChanged();
    update();
}

// ---------------------------------------------------------------------------
// painting
// ---------------------------------------------------------------------------

bool TerminalItem::isSelected(int absLine, int col) const
{
    if (!m_hasSelection)
        return false;
    CellPos a = m_selA, b = m_selB;
    if (b.line < a.line || (b.line == a.line && b.col < a.col))
        std::swap(a, b);
    if (absLine < a.line || absLine > b.line)
        return false;
    if (absLine == a.line && col < a.col)
        return false;
    if (absLine == b.line && col > b.col)
        return false;
    return true;
}

void TerminalItem::paint(QPainter *p)
{
    if (!m_emu)
        return;
    const TerminalTheme &theme = m_emu->theme();
    p->setRenderHint(QPainter::TextAntialiasing, true);
    p->setRenderHint(QPainter::Antialiasing, false);
    if (!theme.bgImage.isEmpty()) {
        // Background picture: scaled to cover, then the theme colour on top
        // at bgDim % so the text stays readable. Cached per size.
        const QSize sz = boundingRect().size().toSize();
        const QString key = theme.bgImage + QLatin1Char('|') + QString::number(sz.width()) + QLatin1Char('x')
                            + QString::number(sz.height()) + (theme.bgFit ? QStringLiteral("|fit") : QString());
        if (key != m_bgKey) {
            m_bgKey = key;
            QImage img(theme.bgImage);
            if (img.isNull() || sz.isEmpty()) {
                m_bgScaled = QImage();
            } else if (!theme.bgFit) {
                m_bgScaled = img.scaled(sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            } else {
                // "Whole picture": the full picture (head to feet) stands on the right,
                // the rest of the terminal is the same picture blurred and darker,
                // so the whole terminal is covered and nothing is cut off.
                QImage canvas(sz, QImage::Format_ARGB32_Premultiplied);
                QPainter cp(&canvas);
                cp.setRenderHint(QPainter::SmoothPixmapTransform, true);
                const QImage cover = img.scaled(sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                // cheap blur: shrink a lot, then stretch back smoothly (twice)
                QImage blur = cover.scaled(std::max(1, sz.width() / 24), std::max(1, sz.height() / 24), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                   .scaled(std::max(1, sz.width() / 6), std::max(1, sz.height() / 6), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                   .scaled(cover.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                cp.drawImage(QPointF((sz.width() - blur.width()) / 2.0, (sz.height() - blur.height()) / 2.0), blur);
                QColor shade = theme.bg;
                shade.setAlphaF(0.45);
                cp.fillRect(QRect(QPoint(0, 0), sz), shade);
                const QImage fit = img.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const qreal x0 = sz.width() - fit.width();
                cp.drawImage(QPointF(x0, (sz.height() - fit.height()) / 2.0), fit);
                // soft left edge of the sharp picture
                const qreal fade = std::min<qreal>(180, fit.width() * 0.3);
                if (x0 > 0) {
                    QImage edge(QSize(int(fade), sz.height()), QImage::Format_ARGB32_Premultiplied);
                    edge.fill(Qt::transparent);
                    {
                        QPainter ep(&edge);
                        ep.drawImage(QPointF(-((sz.width() - blur.width()) / 2.0) - x0, (sz.height() - blur.height()) / 2.0), blur);
                        ep.fillRect(edge.rect(), shade);
                        ep.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                        QLinearGradient g(QPointF(0, 0), QPointF(fade, 0));
                        g.setColorAt(0, QColor(0, 0, 0, 255));
                        g.setColorAt(1, QColor(0, 0, 0, 0));
                        ep.fillRect(edge.rect(), g);
                    }
                    cp.drawImage(QPointF(x0, 0), edge);
                }
                cp.end();
                m_bgScaled = canvas;
            }
        }
        if (!m_bgScaled.isNull()) {
            const QRectF r = boundingRect();
            // "fill": keep the top of the picture (faces are usually there), crop the bottom
            const qreal top = theme.bgFit ? r.center().y() - m_bgScaled.height() / 2.0 : r.top();
            p->drawImage(QPointF(r.center().x() - m_bgScaled.width() / 2.0, top), m_bgScaled);
            QColor dim = theme.bg;
            dim.setAlphaF(theme.bgDim / 100.0);
            p->fillRect(r, dim);
        } else {
            p->fillRect(boundingRect(), theme.bg);
        }
    } else {
        p->fillRect(boundingRect(), theme.bg);
    }

    const int emuRows = m_emu->rows();
    const int emuCols = m_emu->cols();
    const int sb = m_emu->scrollbackSize();
    const int firstLine = sb - m_scrollOffset;
    const int rowsToDraw = std::min(m_rows, emuRows);
    const int colsToDraw = std::min(m_cols, emuCols);

    struct Styled
    {
        QColor fg, bg;
        bool bold = false, italic = false, underline = false, strike = false;
        bool sameText(const Styled &o) const
        {
            return fg == o.fg && bold == o.bold && italic == o.italic && underline == o.underline && strike == o.strike;
        }
    };

    std::vector<Styled> st(static_cast<size_t>(colsToDraw));
    std::vector<QString> txt(static_cast<size_t>(colsToDraw));
    std::vector<int> widths(static_cast<size_t>(colsToDraw), 1);
    VTermScreenCell cell;

    for (int row = 0; row < rowsToDraw; ++row) {
        const int absLine = firstLine + row;
        const qreal y = row * m_ch;

        for (int col = 0; col < colsToDraw; ++col) {
            widths[size_t(col)] = 1;
            if (!m_emu->cellAt(absLine, col, &cell)) {
                txt[size_t(col)] = QStringLiteral(" ");
                st[size_t(col)] = Styled{theme.fg, theme.bg};
                continue;
            }
            Styled s;
            s.fg = m_emu->resolve(cell.fg, true);
            s.bg = m_emu->resolve(cell.bg, false);
            if (cell.attrs.reverse)
                std::swap(s.fg, s.bg);
            if (cell.attrs.conceal)
                s.fg = s.bg;
            s.bold = cell.attrs.bold;
            s.italic = cell.attrs.italic;
            s.underline = cell.attrs.underline != 0;
            s.strike = cell.attrs.strike;
            if (isSelected(absLine, col)) {
                s.bg = theme.selection;
                if (s.fg == theme.bg)
                    s.fg = theme.fg;
            }
            st[size_t(col)] = s;
            txt[size_t(col)] = cell.chars[0] == kWideContinuation ? QString() : cellText(cell);
            widths[size_t(col)] = cell.width > 0 ? cell.width : 1;
        }

        if (m_highlight && !m_emu->altScreen()) {
            // One char per column so regex offsets map straight to columns.
            QString line(colsToDraw, QLatin1Char(' '));
            for (int col = 0; col < colsToDraw; ++col) {
                const QString &t = txt[size_t(col)];
                if (!t.isEmpty())
                    line[col] = t.at(0).isHighSurrogate() ? QChar(0x2588) : t.at(0);
            }
            std::vector<bool> taken(size_t(colsToDraw), false);
            for (const HlRule &rule : hlRules()) {
                auto it = rule.re.globalMatch(line);
                while (it.hasNext()) {
                    const auto m = it.next();
                    const int from = int(m.capturedStart()), to = int(m.capturedEnd());
                    bool free = true;
                    for (int c = from; c < to && free; ++c)
                        free = !taken[size_t(c)];
                    if (!free)
                        continue;
                    for (int c = from; c < to; ++c) {
                        taken[size_t(c)] = true;
                        Styled &s = st[size_t(c)];
                        // only plain text: keep whatever the server coloured itself
                        if (s.fg == theme.fg && s.bg != theme.selection) {
                            s.fg = theme.ansi[rule.ansi];
                            s.bold = s.bold || rule.bold;
                        }
                    }
                }
            }
        }

        for (int col = 0; col < colsToDraw;) {
            int end = col + 1;
            while (end < colsToDraw && st[size_t(end)].bg == st[size_t(col)].bg)
                ++end;
            if (st[size_t(col)].bg != theme.bg) {
                const qreal x0 = std::floor(col * m_cw);
                const qreal x1 = std::ceil(end * m_cw);
                p->fillRect(QRectF(x0, y, x1 - x0, m_ch), st[size_t(col)].bg);
            }
            col = end;
        }

        // search matches on this line
        if (!m_matches.empty()) {
            for (size_t i = 0; i < m_matches.size(); ++i) {
                const Match &m = m_matches[i];
                if (m.line != absLine)
                    continue;
                const bool current = int(i) == m_matchIndex;
                QColor c = current ? theme.ansi[11] : theme.ansi[3];
                c.setAlphaF(current ? 0.85 : 0.45);
                p->fillRect(QRectF(m.col * m_cw, y, m.len * m_cw, m_ch), c);
                if (current)
                    for (int k = m.col; k < m.col + m.len && k < colsToDraw; ++k)
                        st[size_t(k)].fg = theme.bg;
            }
        }

        for (int col = 0; col < colsToDraw;) {
            const QString &t = txt[size_t(col)];
            const Styled &s = st[size_t(col)];
            if (t.isEmpty()) {
                ++col;
                continue;
            }
            const bool ascii = t.size() == 1 && t.at(0).unicode() < 0x7f;
            int end = col + 1;
            QString run = t;
            if (ascii) {
                while (end < colsToDraw) {
                    const QString &n = txt[size_t(end)];
                    if (n.size() != 1 || n.at(0).unicode() >= 0x7f || !st[size_t(end)].sameText(s))
                        break;
                    run += n;
                    ++end;
                }
            }

            QFont f = s.bold ? m_fontBold : m_font;
            f.setItalic(s.italic);
            f.setUnderline(s.underline);
            f.setStrikeOut(s.strike);
            p->setFont(f);
            p->setPen(s.fg);

            const qreal x = col * m_cw;
            if (ascii) {
                if (!run.trimmed().isEmpty() || s.underline || s.strike)
                    p->drawText(QPointF(x, y + m_ascent), run);
            } else {
                const qreal w = widths[size_t(col)] * m_cw;
                p->save();
                p->setClipRect(QRectF(x, y, w, m_ch));
                p->drawText(QPointF(x, y + m_ascent), t);
                p->restore();
                end = col + widths[size_t(col)];
            }
            col = end;
        }
    }

    const VTermPos cur = m_emu->cursor();
    if (m_emu->cursorVisible() && m_scrollOffset == 0 && cur.row < rowsToDraw && cur.col < colsToDraw) {
        const QRectF r(cur.col * m_cw, cur.row * m_ch, m_cw, m_ch);
        if (m_focused) {
            p->fillRect(r, theme.cursor);
            VTermScreenCell c;
            if (m_emu->cellAt(sb + cur.row, cur.col, &c) && c.chars[0] && c.chars[0] != kWideContinuation) {
                p->setFont(c.attrs.bold ? m_fontBold : m_font);
                p->setPen(theme.bg);
                p->drawText(QPointF(r.x(), r.y() + m_ascent), cellText(c));
            }
        } else {
            p->setPen(QPen(theme.cursor, 1));
            p->drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
}

// ---------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------

VTermModifier TerminalItem::modifiers(Qt::KeyboardModifiers m) const
{
    int mod = VTERM_MOD_NONE;
    if (m & Qt::ShiftModifier)
        mod |= VTERM_MOD_SHIFT;
    if (m & Qt::AltModifier)
        mod |= VTERM_MOD_ALT;
    if (m & Qt::ControlModifier)
        mod |= VTERM_MOD_CTRL;
    return VTermModifier(mod);
}

bool TerminalItem::event(QEvent *e)
{
    // Claim every key except the app's own Ctrl+Shift shortcuts, so that
    // Ctrl+W, Ctrl+R, Tab, … reach the remote shell and not the UI.
    if (e->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const auto mods = ke->modifiers();
        const bool appShortcut = (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier);
        if (!appShortcut) {
            e->accept();
            return true;
        }
    }
    return QQuickPaintedItem::event(e);
}

void TerminalItem::keyPressEvent(QKeyEvent *e)
{
    InputGuard inputGuard(m_session);
    const auto qmods = e->modifiers();
    const bool ctrl = qmods & Qt::ControlModifier;
    const bool shift = qmods & Qt::ShiftModifier;
    const bool alt = qmods & Qt::AltModifier;

    // Local actions
    if ((ctrl && shift && e->key() == Qt::Key_C) || (ctrl && !shift && e->key() == Qt::Key_Insert)) {
        copySelection();
        return;
    }
    if ((ctrl && shift && e->key() == Qt::Key_V) || (shift && !ctrl && e->key() == Qt::Key_Insert)) {
        paste();
        return;
    }
    if (shift && !ctrl && e->key() == Qt::Key_PageUp) {
        scrollTo(m_scrollOffset + m_rows - 1);
        return;
    }
    if (shift && !ctrl && e->key() == Qt::Key_PageDown) {
        scrollTo(m_scrollOffset - (m_rows - 1));
        return;
    }
    if (ctrl && (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal)) {
        setFontSize(fontSize() + 1);
        return;
    }
    if (ctrl && e->key() == Qt::Key_Minus) {
        setFontSize(fontSize() - 1);
        return;
    }

    if (!m_session || !m_emu)
        return;

    scrollToBottom();
    if (m_hasSelection) {
        m_hasSelection = false;
        update();
    }

    const VTermModifier mod = modifiers(qmods);
    VTermKey key = VTERM_KEY_NONE;
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:     key = e->modifiers() & Qt::KeypadModifier ? VTERM_KEY_KP_ENTER : VTERM_KEY_ENTER; break;
    case Qt::Key_Tab:       key = VTERM_KEY_TAB; break;
    case Qt::Key_Backtab:   m_emu->key(VTERM_KEY_TAB, VTermModifier(mod | VTERM_MOD_SHIFT)); return;
    case Qt::Key_Backspace: key = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape:    key = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up:        key = VTERM_KEY_UP; break;
    case Qt::Key_Down:      key = VTERM_KEY_DOWN; break;
    case Qt::Key_Left:      key = VTERM_KEY_LEFT; break;
    case Qt::Key_Right:     key = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert:    key = VTERM_KEY_INS; break;
    case Qt::Key_Delete:    key = VTERM_KEY_DEL; break;
    case Qt::Key_Home:      key = VTERM_KEY_HOME; break;
    case Qt::Key_End:       key = VTERM_KEY_END; break;
    case Qt::Key_PageUp:    key = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown:  key = VTERM_KEY_PAGEDOWN; break;
    default:
        if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F12)
            key = VTermKey(VTERM_KEY_FUNCTION(e->key() - Qt::Key_F1 + 1));
        break;
    }
    if (key != VTERM_KEY_NONE) {
        m_emu->key(key, mod);
        return;
    }

    // Ctrl+letter and friends. AltGr on Windows arrives as Ctrl+Alt and
    // produces printable text, which is handled below instead.
    const QString text = e->text();
    const bool printable = !text.isEmpty() && text.at(0).unicode() >= 0x20 && text.at(0).unicode() != 0x7f;
    if (ctrl && !(alt && printable)) {
        const int k = e->key();
        if (k >= Qt::Key_A && k <= Qt::Key_Z) {
            m_emu->unichar(uint32_t('a' + (k - Qt::Key_A)), VTermModifier(mod & ~VTERM_MOD_SHIFT));
            return;
        }
        static const struct { int key; char ch; } extra[] = {
            {Qt::Key_At, '@'}, {Qt::Key_BracketLeft, '['}, {Qt::Key_Backslash, '\\'},
            {Qt::Key_BracketRight, ']'}, {Qt::Key_AsciiCircum, '^'}, {Qt::Key_Underscore, '_'},
            {Qt::Key_Space, ' '}, {Qt::Key_2, '@'}, {Qt::Key_6, '^'}, {Qt::Key_Slash, '_'},
        };
        for (const auto &x : extra) {
            if (k == x.key) {
                m_emu->unichar(uint32_t(x.ch), VTERM_MOD_CTRL);
                return;
            }
        }
    }

    if (printable) {
        const VTermModifier tmod = (alt && !ctrl) ? VTERM_MOD_ALT : VTERM_MOD_NONE;
        const auto ucs = text.toUcs4();
        for (char32_t c : ucs)
            m_emu->unichar(uint32_t(c), tmod);
        return;
    }
    if (!text.isEmpty()) // raw control characters
        writeToSession(text.toUtf8());
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *e)
{
    InputGuard inputGuard(m_session);
    if (!e->commitString().isEmpty() && m_session) {
        scrollToBottom();
        writeToSession(e->commitString().toUtf8());
    }
    e->accept();
}

QVariant TerminalItem::inputMethodQuery(Qt::InputMethodQuery q) const
{
    switch (q) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
        return m_emu ? QRectF(m_emu->cursor().col * m_cw, m_emu->cursor().row * m_ch, m_cw, m_ch) : QRectF();
    case Qt::ImFont:
        return m_font;
    default:
        return QQuickPaintedItem::inputMethodQuery(q);
    }
}

void TerminalItem::focusInEvent(QFocusEvent *e)
{
    m_focused = true;
    update();
    QQuickPaintedItem::focusInEvent(e);
}

void TerminalItem::focusOutEvent(QFocusEvent *e)
{
    m_focused = false;
    update();
    QQuickPaintedItem::focusOutEvent(e);
}

// ---------------------------------------------------------------------------
// mouse, selection, clipboard
// ---------------------------------------------------------------------------

TerminalItem::CellPos TerminalItem::posFromPoint(const QPointF &pt) const
{
    CellPos c;
    const int row = std::clamp(int(pt.y() / m_ch), 0, m_rows - 1);
    c.col = std::clamp(int(pt.x() / m_cw), 0, m_cols - 1);
    c.line = scrollbackSize() - m_scrollOffset + row;
    return c;
}

bool TerminalItem::forwardMouse(QMouseEvent *e, bool pressed, bool move)
{
    // Apps like mc/htop/vim may grab the mouse; Shift bypasses (as in PuTTY).
    if (!m_emu || !m_session || m_emu->mouseMode() == VTERM_PROP_MOUSE_NONE || (e->modifiers() & Qt::ShiftModifier))
        return false;
    const int row = std::clamp(int(e->position().y() / m_ch), 0, m_rows - 1);
    const int col = std::clamp(int(e->position().x() / m_cw), 0, m_cols - 1);
    const VTermModifier mod = modifiers(e->modifiers());
    m_emu->mouseMove(row, col, mod);
    if (!move) {
        int button = 0;
        switch (e->button()) {
        case Qt::LeftButton: button = 1; break;
        case Qt::MiddleButton: button = 2; break;
        case Qt::RightButton: button = 3; break;
        default: break;
        }
        if (button)
            m_emu->mouseButton(button, pressed, mod);
    }
    return true;
}

void TerminalItem::mousePressEvent(QMouseEvent *e)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (forwardMouse(e, true, false))
        return;

    if (e->button() == Qt::LeftButton) {
        m_selecting = true;
        m_hasSelection = false;
        m_selA = m_selB = posFromPoint(e->position());
        update();
    } else if (e->button() == Qt::MiddleButton || (e->button() == Qt::RightButton && m_rightClickPaste)) {
        paste();
    }
}

void TerminalItem::mouseMoveEvent(QMouseEvent *e)
{
    if (forwardMouse(e, false, true))
        return;
    if (!m_selecting)
        return;
    // Auto-scroll when dragging above/below the view.
    if (e->position().y() < 0)
        scrollTo(m_scrollOffset + 1);
    else if (e->position().y() > height())
        scrollTo(m_scrollOffset - 1);
    m_selB = posFromPoint(e->position());
    m_hasSelection = m_selA.line != m_selB.line || m_selA.col != m_selB.col;
    update();
}

void TerminalItem::mouseReleaseEvent(QMouseEvent *e)
{
    if (forwardMouse(e, false, false))
        return;
    if (e->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        if (m_hasSelection && m_copyOnSelect)
            copySelection();
    }
}

void TerminalItem::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (forwardMouse(e, true, false))
        return;
    if (e->button() != Qt::LeftButton)
        return;
    if (!m_emu)
        return;
    const CellPos p = posFromPoint(e->position());
    VTermScreenCell c;
    int a = p.col, b = p.col;
    while (a > 0 && m_emu->cellAt(p.line, a - 1, &c) && isWordChar(c))
        --a;
    while (b < m_cols - 1 && m_emu->cellAt(p.line, b + 1, &c) && isWordChar(c))
        ++b;
    m_selA = {p.line, a};
    m_selB = {p.line, b};
    m_hasSelection = true;
    m_selecting = false;
    if (m_copyOnSelect)
        copySelection();
    update();
}

void TerminalItem::wheelEvent(QWheelEvent *e)
{
    const int steps = e->angleDelta().y() / 120;
    if (steps == 0) {
        e->accept();
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {
        setFontSize(fontSize() + (steps > 0 ? 1 : -1));
        return;
    }
    if (!m_emu)
        return;
    if (m_emu->mouseMode() != VTERM_PROP_MOUSE_NONE && !(e->modifiers() & Qt::ShiftModifier) && m_session) {
        const int row = std::clamp(int(e->position().y() / m_ch), 0, m_rows - 1);
        const int col = std::clamp(int(e->position().x() / m_cw), 0, m_cols - 1);
        m_emu->mouseMove(row, col, VTERM_MOD_NONE);
        for (int i = 0; i < std::abs(steps); ++i)
            m_emu->mouseButton(steps > 0 ? 4 : 5, true, VTERM_MOD_NONE);
        return;
    }
    if (m_emu->altScreen() && m_session) {
        // Full-screen apps without mouse support (less, man): arrows.
        for (int i = 0; i < std::abs(steps) * 3; ++i)
            m_emu->key(steps > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        return;
    }
    scrollTo(m_scrollOffset + steps * 3);
}

// ---------------------------------------------------------------------------
// search
// ---------------------------------------------------------------------------

int TerminalItem::find(const QString &text, bool caseSensitive)
{
    m_matches.clear();
    m_matchIndex = -1;
    if (m_emu && !text.isEmpty()) {
        const int total = m_emu->scrollbackSize() + m_emu->rows();
        const int cols = m_emu->cols();
        const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        VTermScreenCell cell;
        for (int line = 0; line < total; ++line) {
            // one QChar per column, so offsets are columns
            QString row(cols, QLatin1Char(' '));
            bool any = false;
            for (int c = 0; c < cols; ++c) {
                if (m_emu->cellAt(line, c, &cell) && cell.chars[0] && cell.chars[0] != kWideContinuation) {
                    const QString t = cellText(cell);
                    if (!t.isEmpty()) {
                        row[c] = t.at(0);
                        any = true;
                    }
                }
            }
            if (!any)
                continue;
            for (int from = 0; (from = int(row.indexOf(text, from, cs))) >= 0; from += int(text.size()))
                m_matches.push_back({line, from, int(text.size())});
        }
        if (!m_matches.empty())
            m_matchIndex = int(m_matches.size()) - 1;   // the newest one, nearest the prompt
    }
    revealMatch();
    emit findChanged();
    update();
    return int(m_matches.size());
}

void TerminalItem::findNext(bool up)
{
    if (m_matches.empty())
        return;
    const int n = int(m_matches.size());
    m_matchIndex = up ? (m_matchIndex - 1 + n) % n : (m_matchIndex + 1) % n;
    revealMatch();
    emit findChanged();
    update();
}

void TerminalItem::clearFind()
{
    if (m_matches.empty() && m_matchIndex < 0)
        return;
    m_matches.clear();
    m_matchIndex = -1;
    emit findChanged();
    update();
}

void TerminalItem::revealMatch()
{
    if (!m_emu || m_matchIndex < 0)
        return;
    const int line = m_matches[size_t(m_matchIndex)].line;
    const int sb = m_emu->scrollbackSize();
    const int first = sb - m_scrollOffset;
    if (line >= first && line < first + m_rows)
        return;
    // put the match in the middle of the view
    scrollTo(sb - (line - m_rows / 2));
}

void TerminalItem::scrollTo(int offset)
{
    offset = std::clamp(offset, 0, scrollbackSize());
    if (offset == m_scrollOffset)
        return;
    m_scrollOffset = offset;
    emit scrollChanged();
    update();
}

void TerminalItem::scrollToBottom()
{
    scrollTo(0);
}

QString TerminalItem::selectedText() const
{
    if (!m_hasSelection || !m_emu)
        return {};
    CellPos a = m_selA, b = m_selB;
    if (b.line < a.line || (b.line == a.line && b.col < a.col))
        std::swap(a, b);

    QStringList lines;
    VTermScreenCell c;
    for (int line = a.line; line <= b.line; ++line) {
        const int from = line == a.line ? a.col : 0;
        const int to = line == b.line ? b.col : m_cols - 1;
        QString s;
        for (int col = from; col <= to; ++col) {
            if (!m_emu->cellAt(line, col, &c))
                break;
            if (c.chars[0] == kWideContinuation)
                continue;
            s += cellText(c);
        }
        // Trailing blanks are padding, not content.
        int end = int(s.size());
        while (end > 0 && s.at(end - 1) == QLatin1Char(' '))
            --end;
        lines << s.left(end);
    }
    return lines.join(QLatin1Char('\n'));
}

void TerminalItem::copySelection()
{
    const QString t = selectedText();
    if (t.isEmpty())
        return;
    QClipboard *cb = QGuiApplication::clipboard();
    cb->setText(t, QClipboard::Clipboard);
    if (cb->supportsSelection())
        cb->setText(t, QClipboard::Selection);
}

void TerminalItem::paste()
{
    if (!m_session || !m_emu)
        return;
    const QString t = pasteguard::clean(QGuiApplication::clipboard()->text());
    if (t.isEmpty())
        return;
    if (m_confirmPaste && pasteguard::needsConfirm(t)) {
        QString preview = t.left(1500);
        preview.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        emit pasteConfirmRequested(t, pasteguard::lineCount(t), preview);
        return;
    }
    pasteText(t);
}

void TerminalItem::pasteText(const QString &text)
{
    InputGuard inputGuard(m_session);
    if (!m_session || !m_emu)
        return;
    const QString t = pasteguard::clean(text);   // never trust the caller with ESC
    if (t.isEmpty())
        return;
    scrollToBottom();
    m_emu->startPaste(); // emits ESC[200~ only if the app enabled bracketed paste
    writeToSession(t.toUtf8());
    m_emu->endPaste();
}

void TerminalItem::selectAll()
{
    m_selA = {0, 0};
    m_selB = {scrollbackSize() + m_rows - 1, m_cols - 1};
    m_hasSelection = true;
    update();
}

void TerminalItem::clearScrollback()
{
    if (m_emu)
        m_emu->clearScrollback();
    m_scrollOffset = 0;
    m_hasSelection = false;
    emit scrollChanged();
    update();
}
