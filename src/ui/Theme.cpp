#include "Theme.h"

#include "../config/AppPaths.h"
#include "../log/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QPair>

namespace {

ThemeColors darkColors()
{
    return ThemeColors(); // los valores por defecto son el tema oscuro
}

ThemeColors lightColors()
{
    ThemeColors c;
    c.bg = QStringLiteral("#F2F2F5");
    c.panel = QStringLiteral("#FFFFFF");
    c.input = QStringLiteral("#ECECF1");
    c.border = QStringLiteral("#D6D6DE");
    c.text = QStringLiteral("#1B1B1F");
    c.muted = QStringLiteral("#6C6C78");
    c.accent = QStringLiteral("#E53945");
    c.accentHover = QStringLiteral("#D32F3B");
    c.accentText = QStringLiteral("#FFFFFF");
    c.ok = QStringLiteral("#16A05A");
    c.okBg = QStringLiteral("#DDF5E8");
    c.warn = QStringLiteral("#B7791F");
    c.warnBg = QStringLiteral("#FFF1D0");
    c.dangerBg = QStringLiteral("#FDE3E5");
    c.previewBg = QStringLiteral("#1B1B1F");
    c.track = QStringLiteral("#DADAE2");
    return c;
}

ThemeColors make(const char *bg, const char *panel, const char *input, const char *border, const char *text,
                 const char *muted, const char *accent, const char *accentHover, const char *accentText, const char *ok,
                 const char *okBg, const char *warn, const char *warnBg, const char *dangerBg, const char *previewBg,
                 const char *track)
{
    ThemeColors c;
    c.bg = QString::fromLatin1(bg);
    c.panel = QString::fromLatin1(panel);
    c.input = QString::fromLatin1(input);
    c.border = QString::fromLatin1(border);
    c.text = QString::fromLatin1(text);
    c.muted = QString::fromLatin1(muted);
    c.accent = QString::fromLatin1(accent);
    c.accentHover = QString::fromLatin1(accentHover);
    c.accentText = QString::fromLatin1(accentText);
    c.ok = QString::fromLatin1(ok);
    c.okBg = QString::fromLatin1(okBg);
    c.warn = QString::fromLatin1(warn);
    c.warnBg = QString::fromLatin1(warnBg);
    c.dangerBg = QString::fromLatin1(dangerBg);
    c.previewBg = QString::fromLatin1(previewBg);
    c.track = QString::fromLatin1(track);
    return c;
}

// Lista de (nombre en el JSON, campo) para aplicar y generar la hoja de estilo.
QVector<QPair<QString, QString *>> fields(ThemeColors &c)
{
    return {{QStringLiteral("bg"), &c.bg},           {QStringLiteral("panel"), &c.panel},
            {QStringLiteral("input"), &c.input},     {QStringLiteral("border"), &c.border},
            {QStringLiteral("text"), &c.text},       {QStringLiteral("muted"), &c.muted},
            {QStringLiteral("accent"), &c.accent},   {QStringLiteral("accentHover"), &c.accentHover},
            {QStringLiteral("accentText"), &c.accentText}, {QStringLiteral("ok"), &c.ok},
            {QStringLiteral("okBg"), &c.okBg},       {QStringLiteral("warn"), &c.warn},
            {QStringLiteral("warnBg"), &c.warnBg},   {QStringLiteral("dangerBg"), &c.dangerBg},
            {QStringLiteral("previewBg"), &c.previewBg}, {QStringLiteral("track"), &c.track}};
}

bool validColor(const QString &s)
{
    return QColor::isValidColorName(s);
}

const char *kTemplate = R"(
* { font-family: "Segoe UI"; font-size: 13px; color: @text@; }
QMainWindow, QWidget#root { background-color: @bg@; }
QDialog, QTabWidget::pane, QTabWidget QWidget#tab { background-color: @bg@; }
QFrame#card { background-color: @panel@; border: 1px solid @border@; border-radius: 14px; }
QFrame#previewFrame { background-color: @previewBg@; border: 1px solid @border@; border-radius: 14px; }
QLabel { background: transparent; }
QLabel#cardTitle { color: @muted@; font-size: 11px; font-weight: 600; }
QLabel#muted { color: @muted@; }
QLabel#logo { font-size: 24px; font-weight: 800; }
QLabel#time { font-size: 24px; font-weight: 700; font-family: Consolas; min-width: 110px; }
QLabel#placeholder { color: #A0A0A0; font-size: 14px; }
QLabel#statusBar { background-color: @panel@; border: 1px solid @border@; border-radius: 12px; padding: 10px 14px; }
QLabel#statusBar[error="true"] { background-color: @dangerBg@; border: 1px solid @accent@; }
QLabel#pill { border-radius: 11px; padding: 4px 12px; font-weight: 700; background-color: @input@; color: @muted@; }
QLabel#pill[state="rec"] { background-color: @accent@; color: @accentText@; }
QLabel#pill[state="busy"] { background-color: @warnBg@; color: @warn@; }
QLabel#chip { border-radius: 11px; padding: 4px 12px; background-color: @input@; color: @muted@; }
QLabel#chip[state="ok"] { background-color: @okBg@; color: @ok@; }
QLabel#chip[state="wait"] { background-color: @warnBg@; color: @warn@; }
QPushButton { background-color: @input@; border: 1px solid @border@; border-radius: 10px; padding: 8px 14px; }
QPushButton:hover { border-color: @accent@; }
QPushButton:disabled { color: @muted@; border: 1px solid @border@; }
QPushButton#record { background-color: @accent@; border: none; border-radius: 14px; font-size: 16px; font-weight: 700; padding: 16px; color: @accentText@; }
QPushButton#record:hover { background-color: @accentHover@; }
QPushButton#record:disabled { background-color: @input@; color: @muted@; }
QPushButton#record[recording="true"] { background-color: @input@; border: 2px solid @accent@; color: @text@; }
QPushButton#seg { border-radius: 0; padding: 8px 6px; }
QPushButton#seg[pos="left"] { border-top-left-radius: 10px; border-bottom-left-radius: 10px; }
QPushButton#seg[pos="right"] { border-top-right-radius: 10px; border-bottom-right-radius: 10px; }
QPushButton#seg:checked { background-color: @accent@; border-color: @accent@; color: @accentText@; font-weight: 600; }
QPushButton#seg:disabled:checked { background-color: @accent@; color: @accentText@; }
QComboBox, QKeySequenceEdit, QSpinBox, QDoubleSpinBox, QLineEdit { background-color: @input@; border: 1px solid @border@; border-radius: 10px; padding: 7px 10px; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:focus { border-color: @accent@; }
QComboBox:disabled { color: @muted@; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background-color: @panel@; border: 1px solid @border@; selection-background-color: @accent@; selection-color: @accentText@; outline: none; }
QSlider::groove:horizontal { height: 4px; background: @track@; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent@; border-radius: 2px; }
QSlider::handle:horizontal { background: @text@; width: 14px; height: 14px; margin: -6px 0; border-radius: 7px; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 36px; height: 20px; border-radius: 10px; background-color: @track@; }
QCheckBox::indicator:checked { background-color: @accent@; }
QTabBar::tab { background: @input@; border: 1px solid @border@; padding: 8px 16px; border-top-left-radius: 10px; border-top-right-radius: 10px; margin-right: 2px; }
QTabBar::tab:selected { background: @panel@; border-bottom-color: @panel@; font-weight: 600; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
QMenu { background-color: @panel@; border: 1px solid @border@; padding: 6px; border-radius: 8px; }
QMenu::item { padding: 7px 20px; border-radius: 6px; }
QMenu::item:selected { background-color: @accent@; color: @accentText@; }
QMenu::item:disabled { color: @muted@; }
QMenu::separator { height: 1px; background: @border@; margin: 6px 8px; }
QPushButton::menu-indicator { image: none; }
QTextEdit#logView { background-color: @panel@; border: 1px solid @border@; border-radius: 12px; padding: 10px; }
QListWidget#fxList { background-color: @panel@; border: 1px solid @border@; border-radius: 12px; padding: 6px; outline: none; }
QListWidget#fxList::item { padding: 8px; border-radius: 8px; }
QListWidget#fxList::item:selected { background-color: @accent@; color: @accentText@; }
QListWidget#libList { background: transparent; border: none; outline: none; }
QListWidget#libList::item { background-color: @panel@; border: 1px solid @border@; border-radius: 12px; padding: 8px; margin: 4px; color: @text@; }
QListWidget#libList::item:hover { border-color: @accent@; }
QListWidget#libList::item:selected { background-color: @input@; border: 2px solid @accent@; color: @text@; }
QLabel#thumb { background-color: @previewBg@; border-radius: 10px; color: #A0A0A0; }
QPushButton#danger:hover { border-color: @accent@; color: @accent@; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: @track@; border-radius: 5px; min-height: 36px; }
QScrollBar::handle:vertical:hover { background: @accent@; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QMessageBox, QMessageBox QLabel { background-color: @bg@; }
QToolTip { background-color: @panel@; color: @text@; border: 1px solid @border@; padding: 6px; }
)";

} // namespace

namespace Theme {

void ensureSkinsFolder()
{
    const QString dir = AppPaths::skinsDir();
    const QString readme = dir + QStringLiteral("/LEEME.txt");
    if (QFileInfo::exists(readme))
        return;

    QFile r(readme);
    if (r.open(QIODevice::WriteOnly | QIODevice::Text)) {
        r.write("Skins personalizadas de ReKapture\n"
                "=================================\n\n"
                "Copia aqui archivos .json. Al reiniciar ReKapture apareceran en Ajustes > Apariencia.\n\n"
                "Formato:\n"
                "{\n"
                "  \"name\": \"Mi skin\",\n"
                "  \"base\": \"dark\",            (\"dark\" o \"light\": colores de partida)\n"
                "  \"colors\": {\n"
                "    \"accent\": \"#3B82F6\",\n"
                "    \"accentHover\": \"#60A5FA\"\n"
                "  }\n"
                "}\n\n"
                "Opcional: \"emblem\": \"*\" pone un adorno (un emoji) junto al logotipo.\n\n"
                "Colores que se pueden cambiar (todos opcionales, formato #RRGGBB):\n"
                "bg, panel, input, border, text, muted, accent, accentHover, accentText,\n"
                "ok, okBg, warn, warnBg, dangerBg, previewBg, track\n\n"
                "Mira ejemplo-azul.json como referencia.\n");
    }

    QFile e(dir + QStringLiteral("/ejemplo-azul.json"));
    if (e.open(QIODevice::WriteOnly | QIODevice::Text)) {
        e.write("{\n"
                "  \"name\": \"Azul (ejemplo)\",\n"
                "  \"base\": \"dark\",\n"
                "  \"colors\": {\n"
                "    \"accent\": \"#3B82F6\",\n"
                "    \"accentHover\": \"#60A5FA\",\n"
                "    \"okBg\": \"#12303A\"\n"
                "  }\n"
                "}\n");
    }
}

QVector<ThemeInfo> available()
{
    QVector<ThemeInfo> list;
    list.append({QStringLiteral("dark"), QStringLiteral("Oscuro"), darkColors()});
    list.append({QStringLiteral("light"), QStringLiteral("Claro"), lightColors()});
    list.append({QStringLiteral("sakura"), QStringLiteral("Sakura (claro)"),
                 make("#FFF5F8", "#FFFFFF", "#FDE8EF", "#F3CBD9", "#4A2B38", "#9A6F82", "#E8618C", "#D94C79", "#FFFFFF",
                      "#2E9E6B", "#DFF5EA", "#B7791F", "#FFF0D6", "#FFE0E6", "#2A1A22", "#F3D3DF"),
                 QString::fromUtf8("🌸")});
    list.append({QStringLiteral("sakura-night"), QStringLiteral("Sakura nocturno"),
                 make("#1A1216", "#251A20", "#302229", "#4A3340", "#FBEAF0", "#B58CA0", "#FF7FA8", "#FF9BBB", "#2A1019",
                      "#7BE0A8", "#1B3A2B", "#F5C542", "#3A3217", "#3D1824", "#0E090C", "#3A2A33"),
                 QString::fromUtf8("🌸")});
    list.append({QStringLiteral("ocean"), QStringLiteral("Océano"),
                 make("#0E1621", "#152233", "#1D2E44", "#2B4160", "#E6F0FA", "#8FA8C4", "#2F9BFF", "#58B2FF", "#FFFFFF",
                      "#3DDC84", "#12332A", "#F5C542", "#38311A", "#3A1A22", "#070C12", "#24374F"),
                 QString::fromUtf8("🌊")});
    list.append({QStringLiteral("forest"), QStringLiteral("Bosque"),
                 make("#101712", "#17221A", "#1F2D23", "#2E4535", "#E8F3EA", "#93AE99", "#3DBE6E", "#5FD38A", "#0C1A10",
                      "#7BE0A8", "#1B3A2B", "#F5C542", "#3A3217", "#3D1B1B", "#080C09", "#2A3E30"),
                 QString::fromUtf8("🌲")});
    list.append({QStringLiteral("sunset"), QStringLiteral("Atardecer"),
                 make("#1B1320", "#261A2C", "#31233A", "#4A3556", "#FFF0E6", "#BFA1B5", "#FF8A3D", "#FFA45E", "#2A1204",
                      "#7BE0A8", "#1B3A2B", "#F5C542", "#3A3217", "#3D1824", "#0D0810", "#3D2B48"),
                 QString::fromUtf8("🌅")});
    list.append({QStringLiteral("midnight"), QStringLiteral("Medianoche (negro puro)"),
                 make("#000000", "#0D0D0D", "#171717", "#2A2A2A", "#F4F4F4", "#8A8A8A", "#E53945", "#F04B57", "#FFFFFF",
                      "#3DDC84", "#0F2A1E", "#F5C542", "#2E2812", "#2B1013", "#000000", "#1C1C1C"),
                 QString()});

    const QDir dir(AppPaths::skinsDir());
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            Logger::warn(QStringLiteral("Skin ignorada (JSON no válido): %1").arg(fi.fileName()));
            continue;
        }
        const QJsonObject obj = doc.object();
        ThemeColors c = obj.value(QStringLiteral("base")).toString() == QStringLiteral("light") ? lightColors() : darkColors();
        const QJsonObject colors = obj.value(QStringLiteral("colors")).toObject();
        for (auto &field : fields(c)) {
            const QString value = colors.value(field.first).toString();
            if (!value.isEmpty() && validColor(value))
                *field.second = value;
        }
        const QString name = obj.value(QStringLiteral("name")).toString(fi.completeBaseName());
        list.append({QStringLiteral("skin:") + fi.fileName(), QStringLiteral("Skin: ") + name, c,
                     obj.value(QStringLiteral("emblem")).toString().left(4)});
    }
    return list;
}

ThemeInfo find(const QString &id)
{
    const QVector<ThemeInfo> all = available();
    for (const ThemeInfo &t : all)
        if (t.id == id)
            return t;
    return all.first();
}

QString styleSheet(const ThemeColors &colors)
{
    ThemeColors c = colors;
    QString css = QString::fromUtf8(kTemplate);
    for (auto &field : fields(c))
        css.replace(QStringLiteral("@") + field.first + QStringLiteral("@"), *field.second);
    return css;
}

} // namespace Theme
