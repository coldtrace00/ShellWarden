#pragma once

#include <QColor>
#include <QFont>
#include <QPointer>
#include <QImage>
#include <vector>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include "TerminalEmulator.h"

class TerminalSession;

// View of a session's TerminalEmulator: renders the cell grid with QPainter
// and turns keyboard/mouse input into terminal sequences. Scroll position and
// selection belong to the view; the screen content belongs to the session.
//
// PuTTY conventions: selecting text copies it, right click pastes.
class TerminalItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TerminalSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(bool copyOnSelect MEMBER m_copyOnSelect NOTIFY optionsChanged)
    Q_PROPERTY(bool rightClickPaste MEMBER m_rightClickPaste NOTIFY optionsChanged)
    // Colours errors, warnings, IP addresses, URLs, paths and dates in plain
    // (default-coloured) output. Text the server already colours is left alone,
    // and full-screen programs (vim, mc, htop) are never touched.
    Q_PROPERTY(bool highlightOutput READ highlightOutput WRITE setHighlightOutput NOTIFY optionsChanged)
    // search in scrollback + screen (Ctrl+Shift+F)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY findChanged)
    Q_PROPERTY(int matchIndex READ matchIndex NOTIFY findChanged)
    Q_PROPERTY(int scrollbackLines READ scrollbackLines WRITE setScrollbackLines NOTIFY optionsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY themeChanged)
    Q_PROPERTY(bool resizesSession MEMBER m_resizesSession NOTIFY optionsChanged)
    Q_PROPERTY(int columns READ columns NOTIFY gridSizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY gridSizeChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(int scrollOffset READ scrollOffset NOTIFY scrollChanged)
    Q_PROPERTY(int scrollbackSize READ scrollbackSize NOTIFY scrollChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);
    static QString defaultFontFamily();
    ~TerminalItem() override;

    TerminalSession *session() const { return m_session; }
    void setSession(TerminalSession *s);

    QString fontFamily() const { return m_font.family(); }
    void setFontFamily(const QString &family);
    int fontSize() const { return m_font.pointSize(); }
    void setFontSize(int pt);

    QColor backgroundColor() const;
    int columns() const { return m_cols; }
    int rows() const { return m_rows; }
    QString title() const { return m_emu ? m_emu->title() : QString(); }
    int scrollOffset() const { return m_scrollOffset; }
    int scrollbackLines() const { return m_scrollbackMax; }
    void setScrollbackLines(int n);
    int scrollbackSize() const { return m_emu ? m_emu->scrollbackSize() : 0; }

    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearScrollback();
    Q_INVOKABLE void scrollTo(int offset);
    Q_INVOKABLE void sendText(const QString &text);
    Q_INVOKABLE int find(const QString &text, bool caseSensitive);   // returns the number of matches
    Q_INVOKABLE void findNext(bool up);
    Q_INVOKABLE void clearFind();
    int matchCount() const { return int(m_matches.size()); }
    int matchIndex() const { return m_matchIndex; }

    void paint(QPainter *painter) override;
    bool highlightOutput() const { return m_highlight; }
    void setHighlightOutput(bool on);

signals:
    void sessionChanged();
    void fontChanged();
    void optionsChanged();
    void gridSizeChanged();
    void titleChanged();
    void scrollChanged();
    void bell();
    void themeChanged();
    void findChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    bool event(QEvent *e) override;

private:
    struct CellPos { int line = 0; int col = 0; }; // line: absolute (scrollback + screen)

    void updateMetrics();
    void resizeGrid(bool force = false);
    CellPos posFromPoint(const QPointF &p) const;
    bool isSelected(int absLine, int col) const;
    QString selectedText() const;
    VTermModifier modifiers(Qt::KeyboardModifiers m) const;
    bool forwardMouse(QMouseEvent *e, bool pressed, bool move);
    void writeToSession(const QByteArray &data);
    void scrollToBottom();

    QPointer<TerminalSession> m_session;
    QPointer<TerminalEmulator> m_emu;

    QFont m_font;
    QFont m_fontBold;
    qreal m_cw = 8, m_ch = 16, m_ascent = 12;
    int m_cols = 80, m_rows = 24;

    int m_scrollbackMax = 10000;
    int m_scrollOffset = 0;
    bool m_focused = false;
    bool m_resizesSession = true;

    bool m_selecting = false;
    bool m_hasSelection = false;
    CellPos m_selA, m_selB;
    bool m_copyOnSelect = true;
    bool m_rightClickPaste = true;
    bool m_highlight = true;
    struct Match { int line; int col; int len; };
    std::vector<Match> m_matches;
    int m_matchIndex = -1;
    void revealMatch();
    QString m_bgKey;      // background picture cache (path + size)
    QImage m_bgScaled;
};
