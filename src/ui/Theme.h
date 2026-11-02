#pragma once

#include <QString>
#include <QVector>

// Colores de un tema. Cada skin personalizada puede cambiar cualquiera de ellos.
struct ThemeColors {
    QString bg = QStringLiteral("#151515");
    QString panel = QStringLiteral("#202020");
    QString input = QStringLiteral("#2A2A2A");
    QString border = QStringLiteral("#343434");
    QString text = QStringLiteral("#F4F4F4");
    QString muted = QStringLiteral("#A0A0A0");
    QString accent = QStringLiteral("#E53945");
    QString accentHover = QStringLiteral("#F04B57");
    QString accentText = QStringLiteral("#FFFFFF");
    QString ok = QStringLiteral("#3DDC84");
    QString okBg = QStringLiteral("#16362A");
    QString warn = QStringLiteral("#F5C542");
    QString warnBg = QStringLiteral("#3A3217");
    QString dangerBg = QStringLiteral("#3A1518");
    QString previewBg = QStringLiteral("#0C0C0C");
    QString track = QStringLiteral("#2A2A2A");
};

struct ThemeInfo {
    QString id;      // "dark", "light" o "skin:<archivo.json>"
    QString name;    // nombre para mostrar
    ThemeColors colors;
    QString emblem;  // adorno junto al logotipo (por ejemplo una flor de cerezo)
};

namespace Theme {

// Temas integrados + skins .json encontradas en la carpeta de skins del usuario.
QVector<ThemeInfo> available();
ThemeInfo find(const QString &id); // si no existe, devuelve el oscuro
QString styleSheet(const ThemeColors &colors);

// Crea la carpeta de skins con un archivo de ayuda y un ejemplo la primera vez.
void ensureSkinsFolder();

} // namespace Theme
