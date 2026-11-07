#include "TerminalEmulator.h"

#include <QVariantMap>
#include <algorithm>

namespace {

QColor c(const char *hex) { return QColor(QString::fromLatin1(hex)); }

TerminalTheme make(const char *id, const char *name, const char *fg, const char *bg,
                   const char *cursor, const char *sel, std::initializer_list<const char *> ansi)
{
    TerminalTheme t;
    t.id = QString::fromLatin1(id);
    t.name = QString::fromUtf8(name);
    t.fg = c(fg);
    t.bg = c(bg);
    t.cursor = c(cursor);
    t.selection = c(sel);
    int i = 0;
    for (const char *a : ansi)
        t.ansi[i++] = c(a);
    return t;
}

VTermColor toVTerm(const QColor &col)
{
    VTermColor v;
    vterm_color_rgb(&v, uint8_t(col.red()), uint8_t(col.green()), uint8_t(col.blue()));
    return v;
}

} // namespace

namespace {
std::vector<TerminalTheme> &builtinList();
}

const std::vector<TerminalTheme> &TerminalEmulator::themes()
{
    return builtinList();
}

namespace {
std::vector<TerminalTheme> builtinDefaults();
std::vector<TerminalTheme> &builtinList()
{
    static std::vector<TerminalTheme> list = builtinDefaults();
    return list;
}
std::vector<TerminalTheme> builtinDefaults()
{
    return {
        // default: deep blue night, calm accents
        make("default", "Midnight", "#c6d0e3", "#0d1320", "#7dcfff", "#1f2c47",
             {"#172033", "#e0768a", "#7fc8a0", "#e2bf7a", "#6fa3f7", "#a68ff0", "#5fc3d8", "#b7c3da",
              "#4e5d7a", "#f08ea0", "#98dbb5", "#ecd08e", "#8cb8fa", "#bba8f5", "#7fd4e6", "#e9eefb"}),
        // alternative: deep sea blue, teal cursor and IPs (brand colour)
        make("shellwarden", "ShellWarden", "#c6d8e2", "#0b1720", "#4fd1c5", "#1b3140",
             {"#13222d", "#e37b86", "#7fcca0", "#e0c07a", "#68a6e8", "#a493e8", "#4fc9c3", "#b4c8d4",
              "#4a6474", "#f0909a", "#98dcb5", "#ead18e", "#86baf0", "#b8a9ef", "#6fd8d1", "#e6f1f6"}),
        // alternative: darker, a bit more contrast
        make("deepnavy", "Deep Navy", "#cdd7ea", "#0a0f1a", "#7dcfff", "#1b2740",
             {"#141b2b", "#e57a8d", "#82cca3", "#e5c27c", "#72a7fa", "#aa93f2", "#62c7dc", "#bac6dc",
              "#4b5a78", "#f292a3", "#9bdeb8", "#efd391", "#8fbbfc", "#bfacf7", "#82d8ea", "#edf2fc"}),
        // the previous default
        make("classic", "Classic Dark", "#e2e4ed", "#12141c", "#4fd1c5", "#2f3f5c",
             {"#1c1f2b", "#ff5c72", "#4fd18b", "#f5c26b", "#5ea1ff", "#c58cff", "#4fd1c5", "#c9cddc",
              "#545a72", "#ff7a8c", "#6be3a2", "#ffd58a", "#7db4ff", "#d4a6ff", "#6fe0d5", "#ffffff"}),
        // calm graphite: low-saturation colours, readable grey comments, brand teal cursor
        make("graphite", "Graphite", "#cfd5de", "#0f1217", "#4fd1c5", "#263241",
             {"#1a1f27", "#d77b7b", "#8cbf8c", "#d8b56c", "#6f9fd8", "#b08cc9", "#5fb8b2", "#c3cad3",
              "#6a7585", "#ef8f8f", "#a3d9a0", "#ecc985", "#8db8ea", "#c7a6de", "#7fd3cc", "#eef1f5"}),
        make("blueteam", "Blue Team", "#d6e4ff", "#0a0f1c", "#3d8bff", "#1d3157",
             {"#121a2e", "#ff5c72", "#3ddc97", "#ffc857", "#3d8bff", "#9b8cff", "#3ec7ff", "#c8d6f0",
              "#3a4766", "#ff7a8c", "#5ef0ad", "#ffd77f", "#6aa8ff", "#b8adff", "#6fd8ff", "#ffffff"}),
        make("dracula", "Dracula", "#f8f8f2", "#282a36", "#f8f8f2", "#44475a",
             {"#21222c", "#ff5555", "#50fa7b", "#f1fa8c", "#bd93f9", "#ff79c6", "#8be9fd", "#f8f8f2",
              "#6272a4", "#ff6e6e", "#69ff94", "#ffffa5", "#d6acff", "#ff92df", "#a4ffff", "#ffffff"}),
        make("onedark", "One Dark", "#abb2bf", "#282c34", "#528bff", "#3e4451",
             {"#282c34", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#abb2bf",
              "#5c6370", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#ffffff"}),
        make("tokyonight", "Tokyo Night", "#c0caf5", "#1a1b26", "#c0caf5", "#33467c",
             {"#15161e", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#a9b1d6",
              "#414868", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#c0caf5"}),
        make("nord", "Nord", "#d8dee9", "#2e3440", "#d8dee9", "#434c5e",
             {"#3b4252", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#88c0d0", "#e5e9f0",
              "#4c566a", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#8fbcbb", "#eceff4"}),
        make("gruvbox", "Gruvbox Dark", "#ebdbb2", "#282828", "#ebdbb2", "#504945",
             {"#282828", "#cc241d", "#98971a", "#d79921", "#458588", "#b16286", "#689d6a", "#a89984",
              "#928374", "#fb4934", "#b8bb26", "#fabd2f", "#83a598", "#d3869b", "#8ec07c", "#ebdbb2"}),
        make("solarized", "Solarized Dark", "#93a1a1", "#002b36", "#93a1a1", "#0a4b5c",
             {"#073642", "#dc322f", "#859900", "#b58900", "#268bd2", "#d33682", "#2aa198", "#eee8d5",
              "#2c5866", "#cb4b16", "#9bb000", "#d4a000", "#4ea8e8", "#6c71c4", "#3fc1b6", "#fdf6e3"}),
        make("solarizedlight", "Solarized Light", "#586e75", "#fdf6e3", "#586e75", "#e6dfc8",
             {"#eee8d5", "#dc322f", "#859900", "#b58900", "#268bd2", "#d33682", "#2aa198", "#073642",
              "#93a1a1", "#cb4b16", "#586e75", "#657b83", "#839496", "#6c71c4", "#586e75", "#002b36"}),
        make("githublight", "GitHub Light", "#24292f", "#ffffff", "#0969da", "#c8e1ff",
             {"#24292f", "#cf222e", "#116329", "#7d4e00", "#0969da", "#8250df", "#1b7c83", "#6e7781",
              "#57606a", "#a40e26", "#1a7f37", "#633c01", "#218bff", "#a475f9", "#3192aa", "#8c959f"}),
        make("github_dark", "GitHub Dark", "#e6edf3", "#0d1117", "#2f81f7", "#264f78",
             {"#484f58", "#ff7b72", "#3fb950", "#d29922", "#58a6ff", "#bc8cff", "#39c5cf", "#b1bac4",
              "#6e7681", "#ffa198", "#56d364", "#e3b341", "#79c0ff", "#d2a8ff", "#56d4dd", "#ffffff"}),
        make("vscode", "VS Code Dark+", "#cccccc", "#1e1e1e", "#ffffff", "#264f78",
             {"#000000", "#cd3131", "#0dbc79", "#e5e510", "#2472c8", "#bc3fbc", "#11a8cd", "#e5e5e5",
              "#666666", "#f14c4c", "#23d18b", "#f5f543", "#3b8eea", "#d670d6", "#29b8db", "#e5e5e5"}),
        make("catppuccin_mocha", "Catppuccin Mocha", "#cdd6f4", "#1e1e2e", "#f5e0dc", "#585b70",
             {"#45475a", "#f38ba8", "#a6e3a1", "#f9e2af", "#89b4fa", "#f5c2e7", "#94e2d5", "#bac2de",
              "#585b70", "#f38ba8", "#a6e3a1", "#f9e2af", "#89b4fa", "#f5c2e7", "#94e2d5", "#a6adc8"}),
        make("catppuccin_macchiato", "Catppuccin Macchiato", "#cad3f5", "#24273a", "#f4dbd6", "#5b6078",
             {"#494d64", "#ed8796", "#a6da95", "#eed49f", "#8aadf4", "#f5bde6", "#8bd5ca", "#b8c0e0",
              "#5b6078", "#ed8796", "#a6da95", "#eed49f", "#8aadf4", "#f5bde6", "#8bd5ca", "#a5adcb"}),
        make("catppuccin_frappe", "Catppuccin Frappé", "#c6d0f5", "#303446", "#f2d5cf", "#626880",
             {"#51576d", "#e78284", "#a6d189", "#e5c890", "#8caaee", "#f4b8e4", "#81c8be", "#b5bfe2",
              "#626880", "#e78284", "#a6d189", "#e5c890", "#8caaee", "#f4b8e4", "#81c8be", "#a5adce"}),
        make("catppuccin_latte", "Catppuccin Latte", "#4c4f69", "#eff1f5", "#dc8a78", "#acb0be",
             {"#5c5f77", "#d20f39", "#40a02b", "#df8e1d", "#1e66f5", "#ea76cb", "#179299", "#acb0be",
              "#6c6f85", "#d20f39", "#40a02b", "#df8e1d", "#1e66f5", "#ea76cb", "#179299", "#bcc0cc"}),
        make("rosepine", "Rosé Pine", "#e0def4", "#191724", "#524f67", "#403d52",
             {"#26233a", "#eb6f92", "#31748f", "#f6c177", "#9ccfd8", "#c4a7e7", "#ebbcba", "#e0def4",
              "#6e6a86", "#eb6f92", "#31748f", "#f6c177", "#9ccfd8", "#c4a7e7", "#ebbcba", "#e0def4"}),
        make("rosepine_moon", "Rosé Pine Moon", "#e0def4", "#232136", "#59546d", "#44415a",
             {"#393552", "#eb6f92", "#3e8fb0", "#f6c177", "#9ccfd8", "#c4a7e7", "#ea9a97", "#e0def4",
              "#6e6a86", "#eb6f92", "#3e8fb0", "#f6c177", "#9ccfd8", "#c4a7e7", "#ea9a97", "#e0def4"}),
        make("rosepine_dawn", "Rosé Pine Dawn", "#575279", "#faf4ed", "#9893a5", "#dfdad9",
             {"#f2e9e1", "#b4637a", "#286983", "#ea9d34", "#56949f", "#907aa9", "#d7827e", "#575279",
              "#9893a5", "#b4637a", "#286983", "#ea9d34", "#56949f", "#907aa9", "#d7827e", "#575279"}),
        make("kanagawa", "Kanagawa", "#dcd7ba", "#1f1f28", "#c8c093", "#2d4f67",
             {"#16161d", "#c34043", "#76946a", "#c0a36e", "#7e9cd8", "#957fb8", "#6a9589", "#c8c093",
              "#727169", "#e82424", "#98bb6c", "#e6c384", "#7fb4ca", "#938aa9", "#7aa89f", "#dcd7ba"}),
        make("everforest", "Everforest", "#d3c6aa", "#2d353b", "#d3c6aa", "#475258",
             {"#475258", "#e67e80", "#a7c080", "#dbbc7f", "#7fbbb3", "#d699b6", "#83c092", "#d3c6aa",
              "#5c6a72", "#e67e80", "#a7c080", "#dbbc7f", "#7fbbb3", "#d699b6", "#83c092", "#f2efdf"}),
        make("ayu_dark", "Ayu Dark", "#bfbdb6", "#0b0e14", "#e6b450", "#1b3a5b",
             {"#01060e", "#ea6c73", "#7fd962", "#f9af4f", "#53bdfa", "#cda1fa", "#90e1c6", "#c7c7c7",
              "#686868", "#f07178", "#aad94c", "#ffb454", "#59c2ff", "#d2a6ff", "#95e6cb", "#ffffff"}),
        make("ayu_mirage", "Ayu Mirage", "#cccac2", "#1f2430", "#ffcc66", "#33415e",
             {"#171b24", "#ed8274", "#87d96c", "#facc6e", "#6dcbfa", "#dabafa", "#90e1c6", "#c7c7c7",
              "#686868", "#f28779", "#d5ff80", "#ffd173", "#73d0ff", "#dfbfff", "#95e6cb", "#ffffff"}),
        make("ayu_light", "Ayu Light", "#5c6166", "#f8f9fa", "#ffaa33", "#d1e4f4",
             {"#000000", "#ea6c6d", "#6cbf43", "#eca944", "#3199e1", "#9e75c7", "#46ba94", "#8a9199",
              "#686868", "#f07171", "#86b300", "#f2ae49", "#399ee6", "#a37acc", "#4cbf99", "#5c6166"}),
        make("nightowl", "Night Owl", "#d6deeb", "#011627", "#80a4c2", "#1d3b53",
             {"#011627", "#ef5350", "#22da6e", "#addb67", "#82aaff", "#c792ea", "#21c7a8", "#ffffff",
              "#575656", "#ef5350", "#22da6e", "#ffeb95", "#82aaff", "#c792ea", "#7fdbca", "#ffffff"}),
        make("cobalt2", "Cobalt2", "#ffffff", "#193549", "#ffc600", "#0050a4",
             {"#000000", "#ff0000", "#38de21", "#ffe50a", "#1460d2", "#ff005d", "#00bbbb", "#bbbbbb",
              "#555555", "#f40e17", "#3bd01d", "#edc809", "#5555ff", "#ff55ff", "#6ae3fa", "#ffffff"}),
        make("synthwave", "Synthwave '84", "#f0eff1", "#262335", "#f97e72", "#463465",
             {"#000000", "#fe4450", "#72f1b8", "#fede5d", "#03edf9", "#ff7edb", "#03edf9", "#ffffff",
              "#614d85", "#fe4450", "#72f1b8", "#f97e72", "#03edf9", "#ff7edb", "#03edf9", "#ffffff"}),
        make("horizon", "Horizon", "#d5d8da", "#1c1e26", "#e95678", "#2e303e",
             {"#16161c", "#e95678", "#29d398", "#fab795", "#26bbd9", "#ee64ac", "#59e1e3", "#d5d8da",
              "#5b5858", "#ec6a88", "#3fdaa4", "#fbc3a7", "#3fc4de", "#f075b5", "#6be4e6", "#d5d8da"}),
        make("palenight", "Palenight", "#a6accd", "#292d3e", "#ffcc00", "#3c435e",
             {"#292d3e", "#f07178", "#c3e88d", "#ffcb6b", "#82aaff", "#c792ea", "#89ddff", "#d0d0d0",
              "#676e95", "#ff8b92", "#ddffa7", "#ffe585", "#9cc4ff", "#e1acff", "#a3f7ff", "#ffffff"}),
        make("oceanicnext", "Oceanic Next", "#c0c5ce", "#1b2b34", "#c0c5ce", "#343d46",
             {"#343d46", "#ec5f67", "#99c794", "#fac863", "#6699cc", "#c594c5", "#5fb3b3", "#d8dee9",
              "#65737e", "#ec5f67", "#99c794", "#fac863", "#6699cc", "#c594c5", "#5fb3b3", "#ffffff"}),
        make("tokyonight_storm", "Tokyo Night Storm", "#c0caf5", "#24283b", "#c0caf5", "#364a82",
             {"#1d202f", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#a9b1d6",
              "#414868", "#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff", "#c0caf5"}),
        make("tomorrownight", "Tomorrow Night", "#c5c8c6", "#1d1f21", "#c5c8c6", "#373b41",
             {"#1d1f21", "#cc6666", "#b5bd68", "#f0c674", "#81a2be", "#b294bb", "#8abeb7", "#c5c8c6",
              "#969896", "#cc6666", "#b5bd68", "#f0c674", "#81a2be", "#b294bb", "#8abeb7", "#ffffff"}),
        make("zenburn", "Zenburn", "#dcdccc", "#3f3f3f", "#73635a", "#21322f",
             {"#4d4d4d", "#705050", "#60b48a", "#f0dfaf", "#506070", "#dc8cc3", "#8cd0d3", "#dcdccc",
              "#709080", "#dca3a3", "#c3bf9f", "#e0cf9f", "#94bff3", "#ec93d3", "#93e0e3", "#ffffff"}),
        make("gruvboxlight", "Gruvbox Light", "#3c3836", "#fbf1c7", "#3c3836", "#d5c4a1",
             {"#fbf1c7", "#cc241d", "#98971a", "#d79921", "#458588", "#b16286", "#689d6a", "#7c6f64",
              "#928374", "#9d0006", "#79740e", "#b57614", "#076678", "#8f3f71", "#427b58", "#3c3836"}),
        make("onelight", "One Light", "#383a42", "#fafafa", "#526fff", "#e5e5e6",
             {"#383a42", "#e45649", "#50a14f", "#c18401", "#4078f2", "#a626a4", "#0184bc", "#a0a1a7",
              "#696c77", "#e45649", "#50a14f", "#c18401", "#4078f2", "#a626a4", "#0184bc", "#202227"}),
    };
}
} // namespace

namespace {
std::vector<TerminalTheme> &customList()
{
    static std::vector<TerminalTheme> list;
    return list;
}
} // namespace

TerminalTheme TerminalEmulator::themeFromMap(const QVariantMap &m)
{
    // Start from the base theme so missing colours are sensible.
    TerminalTheme t = themeById(m.value(QStringLiteral("base")).toString());
    t.id = m.value(QStringLiteral("id")).toString();
    t.name = m.value(QStringLiteral("name")).toString();
    t.custom = true;
    auto col = [&m](const char *k, const QColor &def) {
        const QColor c(m.value(QString::fromLatin1(k)).toString());
        return c.isValid() ? c : def;
    };
    t.fg = col("fg", t.fg);
    t.bg = col("bg", t.bg);
    t.cursor = col("cursor", t.cursor);
    t.selection = col("selection", t.selection);
    const QVariantList ansi = m.value(QStringLiteral("ansi")).toList();
    for (int i = 0; i < 16 && i < ansi.size(); ++i) {
        const QColor c(ansi.at(i).toString());
        if (c.isValid())
            t.ansi[i] = c;
    }
    t.bgImage = m.value(QStringLiteral("bgImage")).toString();
    t.bgFit = m.value(QStringLiteral("bgFit"), false).toBool();
    t.bgDim = std::clamp(m.value(QStringLiteral("bgDim"), 60).toInt(), 0, 100);
    t.input = col("input", QColor());
    t.autoInput = m.value(QStringLiteral("autoInput")).toBool();
    return t;
}

QString TerminalEmulator::inputColorCommand(const QColor &c)
{
    if (!c.isValid())
        return {};
    // Keeps the original prompt in SW_PS1, so running it again just changes the colour.
    return QStringLiteral(" [ -z \"$SW_PS1\" ] && SW_PS1=\"$PS1\"; PS1=\"$SW_PS1\\[\\e[38;2;%1;%2;%3m\\]\"; "
                          "trap 'printf \"\\e[0m\"' DEBUG")
        .arg(c.red()).arg(c.green()).arg(c.blue());
}

void TerminalEmulator::setCustomThemes(const QVariantList &list)
{
    std::vector<TerminalTheme> out;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("custom-")))
            out.push_back(themeFromMap(m));
    }
    customList() = std::move(out);
}

const TerminalTheme &TerminalEmulator::themeById(const QString &id)
{
    for (const TerminalTheme &t : customList()) {
        if (t.id == id)
            return t;
    }
    for (const TerminalTheme &t : themes()) {
        if (t.id == id)
            return t;
    }
    return themes().front();
}

QVariantList TerminalEmulator::themesForQml()
{
    QVariantList out;
    auto add = [&out](const TerminalTheme &t) {
        QVariantList ansi;
        for (const QColor &a : t.ansi)
            ansi << a.name();
        out << QVariantMap{
            {QStringLiteral("id"), t.id},
            {QStringLiteral("name"), t.name},
            {QStringLiteral("fg"), t.fg.name()},
            {QStringLiteral("bg"), t.bg.name()},
            {QStringLiteral("cursor"), t.cursor.name()},
            {QStringLiteral("selection"), t.selection.name()},
            {QStringLiteral("ansi"), ansi},
            {QStringLiteral("custom"), t.custom},
            {QStringLiteral("bgImage"), t.bgImage},
            {QStringLiteral("bgDim"), t.bgDim},
            {QStringLiteral("bgFit"), t.bgFit},
            {QStringLiteral("input"), t.input.isValid() ? t.input.name() : QString()},
            {QStringLiteral("autoInput"), t.autoInput},
        };
    };
    for (const TerminalTheme &t : customList())
        add(t);
    for (const TerminalTheme &t : themes())
        add(t);
    return out;
}

TerminalEmulator::TerminalEmulator(QObject *parent) : QObject(parent)
{
    m_theme = themes().front();
    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, &TerminalEmulator::cbOutput, this);

    m_screen = vterm_obtain_screen(m_vt);
    static const VTermScreenCallbacks callbacks = [] {
        VTermScreenCallbacks cb{};
        cb.damage = &TerminalEmulator::cbDamage;
        cb.movecursor = &TerminalEmulator::cbMoveCursor;
        cb.settermprop = &TerminalEmulator::cbSetTermProp;
        cb.bell = &TerminalEmulator::cbBell;
        cb.sb_pushline = &TerminalEmulator::cbSbPushLine;
        cb.sb_popline = &TerminalEmulator::cbSbPopLine;
#if VTERM_VERSION_MAJOR > 0 || VTERM_VERSION_MINOR >= 3
        cb.sb_clear = &TerminalEmulator::cbSbClear;
#endif
        return cb;
    }();
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(m_screen, 1);
#if VTERM_VERSION_MAJOR > 0 || VTERM_VERSION_MINOR >= 3
    vterm_screen_enable_reflow(m_screen, true);
#endif
    applyPalette();
    vterm_screen_reset(m_screen, 1);
}

TerminalEmulator::~TerminalEmulator()
{
    if (m_vt)
        vterm_free(m_vt);
}

void TerminalEmulator::applyPalette()
{
    VTermState *state = vterm_obtain_state(m_vt);
    const VTermColor fg = toVTerm(m_theme.fg);
    const VTermColor bg = toVTerm(m_theme.bg);
    vterm_state_set_default_colors(state, &fg, &bg);
    for (int i = 0; i < 16; ++i) {
        VTermColor col = toVTerm(m_theme.ansi[i]);
        vterm_state_set_palette_color(state, i, &col);
    }
    vterm_state_set_bold_highbright(state, 1);
}

void TerminalEmulator::setTheme(const QString &id)
{
    const TerminalTheme &t = themeById(id);
    if (t.id == m_theme.id && !t.custom)
        return;
    m_theme = t;
    applyPalette();
    emit themeChanged();
    emit damaged();
}

void TerminalEmulator::reloadTheme()
{
    m_theme = themeById(m_theme.id);
    applyPalette();
    emit themeChanged();
    emit damaged();
}

void TerminalEmulator::feed(const QByteArray &data)
{
    vterm_input_write(m_vt, data.constData(), size_t(data.size()));
    vterm_screen_flush_damage(m_screen);
    emit damaged();
}

void TerminalEmulator::resize(int rows, int cols)
{
    rows = std::max(2, rows);
    cols = std::max(10, cols);
    if (rows == m_rows && cols == m_cols)
        return;
    const int oldRows = m_rows, oldCols = m_cols;
    m_rows = rows;
    m_cols = cols;

    // libvterm 0.3.x reflows the text correctly but often leaves the cursor a
    // row (or a column) off. The shell then redraws its prompt over the
    // previous output. Remember the cursor as a position inside its logical
    // line, counted from the last line with text, and restore it afterwards.
    VTermState *state = vterm_obtain_state(m_vt);
    const bool fix = !m_altScreen;
    bool belowText = false;
    int logicalBelow = 0, offset = 0, rowsBelow = 0;
    if (fix) {
        VTermPos cp;
        vterm_state_get_cursorpos(state, &cp);
        const int last = lastTextRow(oldRows, oldCols);
        if (last < 0 || cp.row > last) {
            belowText = true;
            rowsBelow = cp.row - last;
            offset = cp.col;
        } else {
            int start = cp.row;
            while (start > 0 && isContinuation(start))
                --start;
            offset = (cp.row - start) * oldCols + cp.col;
            for (int r = start + 1; r <= last; ++r)
                if (!isContinuation(r))
                    ++logicalBelow;
        }
    }

    vterm_set_size(m_vt, rows, cols);

    if (fix) {
        const int last = lastTextRow(rows, cols);
        int row, col;
        if (belowText) {
            row = last + rowsBelow;
            col = offset;
        } else {
            int r = std::max(0, last);
            while (r > 0 && isContinuation(r))
                --r;
            for (int i = 0; i < logicalBelow && r > 0; ++i) {
                --r;
                while (r > 0 && isContinuation(r))
                    --r;
            }
            row = r + offset / cols;
            col = offset % cols;
        }
        row = std::clamp(row, 0, rows - 1);
        col = std::clamp(col, 0, cols - 1);
        const QByteArray seq = QByteArrayLiteral("\x1b[") + QByteArray::number(row + 1) + ';'
                               + QByteArray::number(col + 1) + 'H';
        vterm_input_write(m_vt, seq.constData(), size_t(seq.size()));   // local only
    }
    vterm_screen_flush_damage(m_screen);
    emit damaged();
}

QString TerminalEmulator::cursorLineText() const
{
    int start = m_cursor.row;
    while (start > 0 && isContinuation(start))
        --start;
    QString out;
    for (int r = start; r <= m_cursor.row && r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            VTermScreenCell cell;
            if (!vterm_screen_get_cell(m_screen, VTermPos{r, c}, &cell))
                continue;
            if (cell.chars[0] == kWideContinuation)
                continue;
            if (cell.chars[0] == 0) {
                out += QLatin1Char(' ');
                continue;
            }
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) {
                const char32_t cp = cell.chars[i];
                out += QString::fromUcs4(&cp, 1);
            }
        }
    }
    while (out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    return out;
}

QString TerminalEmulator::lineText(int absLine, bool *full) const
{
    QString out;
    bool last = false;
    for (int c = 0; c < m_cols; ++c) {
        VTermScreenCell cell;
        if (!cellAt(absLine, c, &cell) || cell.chars[0] == kWideContinuation)
            continue;
        if (cell.chars[0] == 0) {
            out += QLatin1Char(' ');
            last = false;
            continue;
        }
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) {
            const char32_t cp = cell.chars[i];
            out += QString::fromUcs4(&cp, 1);
        }
        last = c == m_cols - 1 && cell.chars[0] != ' ';
    }
    if (full)
        *full = last;
    while (out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    return out;
}

int TerminalEmulator::cursorLineStart() const
{
    int start = m_cursor.row;
    while (start > 0 && isContinuation(start))
        --start;
    return scrollbackSize() + start;
}

bool TerminalEmulator::isContinuation(int row) const
{
    const VTermLineInfo *li = vterm_state_get_lineinfo(vterm_obtain_state(m_vt), row);
    return li && li->continuation;
}

int TerminalEmulator::lastTextRow(int rows, int cols) const
{
    for (int r = rows - 1; r >= 0; --r) {
        for (int c = 0; c < cols; ++c) {
            VTermScreenCell cell;
            if (vterm_screen_get_cell(m_screen, VTermPos{r, c}, &cell) && cell.chars[0] && cell.chars[0] != ' ')
                return r;
        }
    }
    return -1;
}

void TerminalEmulator::reset()
{
    vterm_screen_reset(m_screen, 1);
    m_scrollback.clear();
    emit scrollbackChanged();
    emit damaged();
}

void TerminalEmulator::key(VTermKey k, VTermModifier mod) { vterm_keyboard_key(m_vt, k, mod); }
void TerminalEmulator::unichar(uint32_t ch, VTermModifier mod) { vterm_keyboard_unichar(m_vt, ch, mod); }
void TerminalEmulator::mouseMove(int row, int col, VTermModifier mod) { vterm_mouse_move(m_vt, row, col, mod); }
void TerminalEmulator::mouseButton(int b, bool pressed, VTermModifier mod) { vterm_mouse_button(m_vt, b, pressed, mod); }
void TerminalEmulator::startPaste() { vterm_keyboard_start_paste(m_vt); }
void TerminalEmulator::endPaste() { vterm_keyboard_end_paste(m_vt); }

bool TerminalEmulator::cellAt(int absLine, int col, VTermScreenCell *cell) const
{
    const int sb = int(m_scrollback.size());
    if (absLine < 0)
        return false;
    if (absLine < sb) {
        const auto &line = m_scrollback[size_t(absLine)];
        if (col < int(line.size())) {
            *cell = line[size_t(col)];
            return true;
        }
        *cell = VTermScreenCell{};
        cell->width = 1;
        cell->fg.type = VTERM_COLOR_DEFAULT_FG;
        cell->bg.type = VTERM_COLOR_DEFAULT_BG;
        return true;
    }
    VTermPos pos{absLine - sb, col};
    if (pos.row >= m_rows || col >= m_cols)
        return false;
    return vterm_screen_get_cell(m_screen, pos, cell) != 0;
}

QColor TerminalEmulator::resolve(const VTermColor &col, bool fg) const
{
    if (fg && VTERM_COLOR_IS_DEFAULT_FG(&col))
        return m_theme.fg;
    if (!fg && VTERM_COLOR_IS_DEFAULT_BG(&col))
        return m_theme.bg;
    VTermColor tmp = col;
    vterm_screen_convert_color_to_rgb(m_screen, &tmp);
    return QColor(tmp.rgb.red, tmp.rgb.green, tmp.rgb.blue);
}

void TerminalEmulator::clearScrollback()
{
    m_scrollback.clear();
    emit scrollbackChanged();
    emit damaged();
}

void TerminalEmulator::setScrollbackLimit(int lines)
{
    m_scrollbackMax = std::max(0, lines);
    while (int(m_scrollback.size()) > m_scrollbackMax)
        m_scrollback.pop_front();
    emit scrollbackChanged();
}

// ---------------------------------------------------------------------------
// libvterm callbacks
// ---------------------------------------------------------------------------

void TerminalEmulator::cbOutput(const char *s, size_t len, void *user)
{
    emit static_cast<TerminalEmulator *>(user)->output(QByteArray(s, int(len)));
}

int TerminalEmulator::cbDamage(VTermRect, void *) { return 1; }

int TerminalEmulator::cbMoveCursor(VTermPos pos, VTermPos, int visible, void *user)
{
    auto *self = static_cast<TerminalEmulator *>(user);
    self->m_cursor = pos;
    self->m_cursorVisible = visible != 0;
    return 1;
}

int TerminalEmulator::cbSetTermProp(VTermProp prop, VTermValue *val, void *user)
{
    auto *self = static_cast<TerminalEmulator *>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        self->m_cursorVisible = val->boolean;
        break;
    case VTERM_PROP_ALTSCREEN:
        self->m_altScreen = val->boolean;
        emit self->scrollbackChanged();
        break;
    case VTERM_PROP_MOUSE:
        self->m_mouseMode = val->number;
        break;
    case VTERM_PROP_TITLE:
#if VTERM_VERSION_MAJOR > 0 || VTERM_VERSION_MINOR >= 2
        if (val->string.initial)
            self->m_titleBuf.clear();
        self->m_titleBuf.append(val->string.str, int(val->string.len));
        if (val->string.final) {
            self->m_title = QString::fromUtf8(self->m_titleBuf).left(200);
            emit self->titleChanged();
        }
#else
        self->m_title = QString::fromUtf8(val->string);
        emit self->titleChanged();
#endif
        break;
    default:
        break;
    }
    return 1;
}

int TerminalEmulator::cbBell(void *user)
{
    emit static_cast<TerminalEmulator *>(user)->bell();
    return 1;
}

int TerminalEmulator::cbSbPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalEmulator *>(user);
    if (self->m_scrollbackMax <= 0)
        return 1;
    self->m_scrollback.emplace_back(cells, cells + cols);
    if (int(self->m_scrollback.size()) > self->m_scrollbackMax)
        self->m_scrollback.pop_front();
    else
        emit self->scrollbackPushed();
    emit self->scrollbackChanged();
    return 1;
}

int TerminalEmulator::cbSbPopLine(int cols, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalEmulator *>(user);
    if (self->m_scrollback.empty())
        return 0;
    const std::vector<VTermScreenCell> &line = self->m_scrollback.back();
    VTermScreenCell blank{};
    blank.width = 1;
    blank.fg.type = VTERM_COLOR_DEFAULT_FG;
    blank.bg.type = VTERM_COLOR_DEFAULT_BG;
    for (int i = 0; i < cols; ++i)
        cells[i] = i < int(line.size()) ? line[size_t(i)] : blank;
    self->m_scrollback.pop_back();
    emit self->scrollbackChanged();
    return 1;
}

int TerminalEmulator::cbSbClear(void *user)
{
    static_cast<TerminalEmulator *>(user)->clearScrollback();
    return 1;
}
