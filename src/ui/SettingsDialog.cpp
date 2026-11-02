#include "SettingsDialog.h"

#include "../config/AppPaths.h"
#include "../obs/ObsEngine.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct ResolutionItem { int w; int h; const char *label; };
const ResolutionItem kResolutions[] = {
    {1280, 720, "1280 × 720 (720p)"},
    {1920, 1080, "1920 × 1080 (1080p)"},
    {2560, 1440, "2560 × 1440 (1440p)"},
    {3840, 2160, "3840 × 2160 (4K)"},
};

QLabel *note(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setObjectName(QStringLiteral("muted"));
    return l;
}

QWidget *makeTab(QFormLayout **formOut, QWidget *parent)
{
    auto *w = new QWidget(parent);
    w->setObjectName(QStringLiteral("tab"));
    auto *outer = new QVBoxLayout(w);
    outer->setContentsMargins(16, 16, 16, 16);
    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setVerticalSpacing(12);
    outer->addLayout(form);
    outer->addStretch(1);
    *formOut = form;
    return w;
}

} // namespace

SettingsDialog::SettingsDialog(const AppSettingsData &current, const QVector<EncoderChoice> &encoders, QWidget *parent)
    : QDialog(parent)
    , m_result(current)
    , m_encoders(encoders)
{
    setWindowTitle(QStringLiteral("Ajustes de ReKapture"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildVideoTab(), QStringLiteral("Video"));
    tabs->addTab(buildRecordingTab(), QStringLiteral("Grabación"));
    tabs->addTab(buildAudioTab(), QStringLiteral("Audio y webcam"));
    tabs->addTab(buildClipsTab(), QStringLiteral("Clips y repetición"));
    tabs->addTab(buildHotkeysTab(), QStringLiteral("Atajos"));
    tabs->addTab(buildAppearanceTab(), QStringLiteral("Apariencia"));
    layout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons);

    updateNotes();
}

QWidget *SettingsDialog::buildVideoTab()
{
    QFormLayout *form = nullptr;
    QWidget *tab = makeTab(&form, this);

    m_resolution = new QComboBox(tab);
    for (const ResolutionItem &r : kResolutions)
        m_resolution->addItem(QString::fromUtf8(r.label), QSize(r.w, r.h));
    for (int i = 0; i < m_resolution->count(); ++i)
        if (m_resolution->itemData(i).toSize() == QSize(m_result.width, m_result.height))
            m_resolution->setCurrentIndex(i);
    form->addRow(QStringLiteral("Resolución de salida:"), m_resolution);

    m_fps = new QComboBox(tab);
    m_fps->addItem(QStringLiteral("30 FPS"), 30);
    m_fps->addItem(QStringLiteral("60 FPS"), 60);
    m_fps->setCurrentIndex(m_result.fps == 30 ? 0 : 1);
    form->addRow(QStringLiteral("Fotogramas por segundo:"), m_fps);

    m_encoder = new QComboBox(tab);
    m_encoder->addItem(QStringLiteral("Automático (recomendado)"), QStringLiteral("auto"));
    for (const EncoderChoice &c : m_encoders)
        m_encoder->addItem(c.label, c.key);
    // Los codificadores no disponibles se muestran deshabilitados, con su explicacion.
    if (auto *model = qobject_cast<QStandardItemModel *>(m_encoder->model())) {
        for (int i = 0; i < m_encoders.size(); ++i) {
            if (m_encoders[i].available)
                continue;
            if (QStandardItem *item = model->item(i + 1)) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setData(m_encoders[i].reason, Qt::ToolTipRole);
            }
        }
    }
    for (int i = 0; i < m_encoder->count(); ++i)
        if (m_encoder->itemData(i).toString() == m_result.encoderKey)
            m_encoder->setCurrentIndex(i);
    form->addRow(QStringLiteral("Codificador de video:"), m_encoder);

    m_encoderNote = note(QString(), tab);
    form->addRow(QString(), m_encoderNote);

    m_quality = new QComboBox(tab);
    for (const QString &q : EncoderCatalog::qualityLabels())
        m_quality->addItem(q);
    m_quality->setCurrentIndex(m_result.quality);
    form->addRow(QStringLiteral("Perfil de calidad:"), m_quality);

    m_bitrateMode = new QComboBox(tab);
    m_bitrateMode->addItem(QStringLiteral("Automático (según el perfil de calidad)"), 0);
    m_bitrateMode->addItem(QStringLiteral("Personalizado"), 1);
    m_bitrateMode->setCurrentIndex(m_result.bitrateCustom ? 1 : 0);
    form->addRow(QStringLiteral("Bitrate de video:"), m_bitrateMode);

    m_bitrateSpin = new QSpinBox(tab);
    m_bitrateSpin->setRange(1000, 150000);
    m_bitrateSpin->setSingleStep(500);
    m_bitrateSpin->setSuffix(QStringLiteral(" kbps"));
    m_bitrateSpin->setValue(m_result.bitrateKbps);
    form->addRow(QStringLiteral("Bitrate personalizado:"), m_bitrateSpin);

    m_bitrateNote = note(QString(), tab);
    form->addRow(QString(), m_bitrateNote);

    m_audioBitrate = new QComboBox(tab);
    for (int kbps : {96, 128, 160, 192, 256, 320})
        m_audioBitrate->addItem(QStringLiteral("%1 kbps").arg(kbps), kbps);
    for (int i = 0; i < m_audioBitrate->count(); ++i)
        if (m_audioBitrate->itemData(i).toInt() == m_result.audioBitrateKbps)
            m_audioBitrate->setCurrentIndex(i);
    form->addRow(QStringLiteral("Bitrate de audio (por pista):"), m_audioBitrate);

    auto refresh = [this] { updateNotes(); };
    connect(m_encoder, &QComboBox::currentIndexChanged, this, refresh);
    connect(m_quality, &QComboBox::currentIndexChanged, this, refresh);
    connect(m_resolution, &QComboBox::currentIndexChanged, this, refresh);
    connect(m_fps, &QComboBox::currentIndexChanged, this, refresh);
    connect(m_bitrateMode, &QComboBox::currentIndexChanged, this, refresh);
    connect(m_bitrateSpin, &QSpinBox::valueChanged, this, [this] { updateNotes(); });
    return tab;
}

QWidget *SettingsDialog::buildRecordingTab()
{
    QFormLayout *form = nullptr;
    QWidget *tab = makeTab(&form, this);

    m_organizeByGame = new QCheckBox(QStringLiteral("Organizar las grabaciones por juego (carpeta y nombre)"), tab);
    m_organizeByGame->setChecked(m_result.organizeByGame);
    form->addRow(m_organizeByGame);
    form->addRow(note(QStringLiteral("Crea una carpeta por juego dentro de la carpeta de grabaciones y usa su nombre en los archivos. "
                                     "El nombre se toma del juego capturado, de la ventana elegida o de la ventana en primer plano "
                                     "al empezar. Los clips van a una subcarpeta «Clips»."), tab));

    m_soundCues = new QCheckBox(QStringLiteral("Reproducir un aviso sonoro al iniciar, detener y guardar clips"), tab);
    m_soundCues->setChecked(m_result.soundCues);
    m_soundCues->setToolTip(QStringLiteral("El aviso de inicio suena antes de grabar y el de parada cuando los archivos ya están cerrados, "
                                           "así que no queda en la grabación."));
    form->addRow(m_soundCues);

    m_overlayEnabled = new QCheckBox(QStringLiteral("Mostrar un icono de actividad en pantalla"), tab);
    m_overlayEnabled->setChecked(m_result.overlayEnabled);
    form->addRow(m_overlayEnabled);

    m_overlayCorner = new QComboBox(tab);
    m_overlayCorner->addItem(QStringLiteral("Arriba a la izquierda"), 0);
    m_overlayCorner->addItem(QStringLiteral("Arriba a la derecha"), 1);
    m_overlayCorner->addItem(QStringLiteral("Abajo a la izquierda"), 2);
    m_overlayCorner->addItem(QStringLiteral("Abajo a la derecha"), 3);
    m_overlayCorner->setCurrentIndex(qBound(0, m_result.overlayCorner, 3));
    form->addRow(QStringLiteral("Posición del icono:"), m_overlayCorner);
    form->addRow(note(QStringLiteral("Pequeña etiqueta que indica si se está grabando, si el micrófono o la webcam están incluidos y si "
                                     "la repetición está activa. Nunca aparece en las grabaciones. En pantalla completa exclusiva "
                                     "puede no verse; en ventana sin bordes sí."), tab));

    m_hideApp = new QCheckBox(QStringLiteral("Ocultar la ventana de ReKapture en las grabaciones y capturas de pantalla"), tab);
    m_hideApp->setChecked(m_result.hideAppFromCapture);
    form->addRow(m_hideApp);
    form->addRow(note(QStringLiteral("Desactivado: la ventana de ReKapture sale en tus grabaciones y capturas de pantalla, útil para "
                                     "enseñar el programa. Al grabar un monitor con esta ventana visible, la previsualización se "
                                     "pausa sola para evitar el efecto espejo. El icono de actividad nunca se graba."), tab));

    form->addRow(note(QStringLiteral("Pausar y reanudar: no disponible todavía (con dos archivos sincronizados se evaluará en la "
                                     "etapa de fiabilidad)."), tab));
    return tab;
}

QWidget *SettingsDialog::buildAudioTab()
{
    QFormLayout *form = nullptr;
    QWidget *tab = makeTab(&form, this);

    m_micExport = new QComboBox(tab);
    m_micExport->addItem(QStringLiteral("Solo en un archivo aparte"), 0);
    m_micExport->addItem(QStringLiteral("Solo como segunda pista del vídeo (multipista)"), 1);
    m_micExport->addItem(QStringLiteral("Ambos"), 2);
    m_micExport->setCurrentIndex(qBound(0, m_result.micExportMode, 2));
    form->addRow(QStringLiteral("Micrófono:"), m_micExport);
    form->addRow(note(QStringLiteral("Multipista: el vídeo lleva dos pistas de audio, la 1 con el audio del escritorio y la 2 con el "
                                     "micrófono; un editor de vídeo las muestra por separado. El archivo aparte contiene solo el micrófono."), tab));

    m_micFormat = new QComboBox(tab);
    m_micFormat->addItem(QStringLiteral("WAV (sin pérdida)"), 0);
    m_micFormat->addItem(QStringLiteral("MP3 (192 kbps)"), 1);
    m_micFormat->addItem(QStringLiteral("MP4 con imagen negra (compatibilidad)"), 2);
    m_micFormat->setCurrentIndex(qBound(0, m_result.micFileFormat, 2));
    form->addRow(QStringLiteral("Formato del archivo aparte:"), m_micFormat);
    form->addRow(note(QStringLiteral("El MP3 lo crea el codificador que trae Windows; si tu Windows no lo tiene (ediciones N) se guarda "
                                     "en WAV. El WAV ocupa unos 11 MB por minuto."), tab));

    m_webcamRes = new QComboBox(tab);
    m_webcamRes->addItem(QStringLiteral("1280 × 720 (30 FPS de cámara)"), 0);
    m_webcamRes->addItem(QStringLiteral("1920 × 1080 (30 FPS de cámara)"), 1);
    m_webcamRes->setCurrentIndex(qBound(0, m_result.webcamResolution, 1));
    form->addRow(QStringLiteral("Webcam:"), m_webcamRes);

    m_webcamAudio = new QCheckBox(QStringLiteral("Incluir el micrófono en el MP4 de la webcam"), tab);
    m_webcamAudio->setChecked(m_result.webcamWithAudio);
    form->addRow(m_webcamAudio);
    form->addRow(note(QStringLiteral("La webcam siempre se guarda en su propio MP4 (…_CAM.mp4) y nunca se mezcla con el vídeo de la "
                                     "pantalla. La cámara, su vista previa y la activación están en la ventana principal."), tab));

    m_vstEnabled = new QCheckBox(QStringLiteral("Activar plugins VST 2.x externos"), tab);
    m_vstEnabled->setChecked(m_result.vstEnabled);
    form->addRow(m_vstEnabled);
    form->addRow(note(QStringLiteral("Requiere reiniciar ReKapture. Se admiten plugins VST 2.x de 64 bits instalados en las carpetas "
                                     "habituales de plugins VST de Windows (los VST3 no son compatibles). Después se añaden con el botón "
                                     "«Efectos» del micrófono o del escritorio."), tab));

    auto *retry = new QPushButton(QStringLiteral("Reintentar módulos desactivados"), tab);
    retry->setToolTip(QStringLiteral("Si un módulo opcional (webcam o VST) cerró la aplicación al cargarse, ReKapture lo desactiva "
                                     "en el siguiente arranque. Este botón vuelve a permitir su carga."));
    connect(retry, &QPushButton::clicked, this, [this] {
        ObsEngine::clearModuleGuards();
        QMessageBox::information(this, windowTitle(), QStringLiteral("Hecho. Reinicia ReKapture para volver a intentarlo."));
    });
    form->addRow(QString(), retry);
    return tab;
}

QWidget *SettingsDialog::buildClipsTab()
{
    QFormLayout *form = nullptr;
    QWidget *tab = makeTab(&form, this);

    m_clipsWhileRecording = new QCheckBox(QStringLiteral("Permitir guardar clips mientras se graba"), tab);
    m_clipsWhileRecording->setChecked(m_result.clipsWhileRecording);
    form->addRow(m_clipsWhileRecording);
    form->addRow(note(QStringLiteral("El buffer arranca junto a la grabación y comparte el codificador: casi no añade carga, "
                                     "solo memoria."), tab));

    m_instantReplay = new QCheckBox(QStringLiteral("Repetición instantánea (buffer siempre activo aunque no estés grabando)"), tab);
    m_instantReplay->setChecked(m_result.instantReplay);
    form->addRow(m_instantReplay);
    form->addRow(note(QStringLiteral("Mantiene la captura y un codificador funcionando todo el tiempo. Si notas bajadas de FPS, "
                                     "desactívala: también puedes alternarla con su atajo o desde la ventana principal."), tab));

    m_clipSeconds = new QSpinBox(tab);
    m_clipSeconds->setRange(5, 600);
    m_clipSeconds->setSuffix(QStringLiteral(" s"));
    m_clipSeconds->setValue(m_result.clipSeconds);
    form->addRow(QStringLiteral("Duración del clip:"), m_clipSeconds);

    m_memoryNote = note(QString(), tab);
    form->addRow(QString(), m_memoryNote);

    m_autoDisableReplay = new QCheckBox(QStringLiteral("Apagar la repetición automáticamente si se pierden fotogramas"), tab);
    m_autoDisableReplay->setChecked(m_result.autoDisableReplay);
    form->addRow(m_autoDisableReplay);
    form->addRow(note(QStringLiteral("Si más del 5 % de los fotogramas se pierde durante 10 segundos con la repetición "
                                     "instantánea activa, se desactiva sola para no molestar mientras juegas."), tab));

    connect(m_clipSeconds, &QSpinBox::valueChanged, this, [this] { updateNotes(); });
    return tab;
}

QWidget *SettingsDialog::buildHotkeysTab()
{
    QFormLayout *form = nullptr;
    QWidget *tab = makeTab(&form, this);

    auto makeKey = [&](const QString &value, const QString &label) {
        auto *k = new QKeySequenceEdit(QKeySequence::fromString(value, QKeySequence::PortableText), tab);
        k->setMaximumSequenceLength(1);
        form->addRow(label, k);
        return k;
    };
    m_startKey = makeKey(m_result.startHotkey, QStringLiteral("Iniciar grabación:"));
    m_stopKey = makeKey(m_result.stopHotkey, QStringLiteral("Detener grabación:"));
    m_clipKey = makeKey(m_result.clipHotkey, QStringLiteral("Guardar clip:"));
    m_replayKey = makeKey(m_result.replayHotkey, QStringLiteral("Activar/desactivar repetición:"));
    form->addRow(note(QStringLiteral("Funcionan aunque ReKapture no tenga el foco. Usa Ctrl, Alt o Mayús junto a una tecla "
                                     "(o una tecla F). Si Windows no deja registrar una combinación, ReKapture te lo avisa."), tab));
    return tab;
}

QWidget *SettingsDialog::buildAppearanceTab()
{
    QFormLayout *form = nullptr;
    QWidget *tab = makeTab(&form, this);

    m_theme = new QComboBox(tab);
    Theme::ensureSkinsFolder();
    const QVector<ThemeInfo> themes = Theme::available();
    for (const ThemeInfo &t : themes)
        m_theme->addItem(t.name, t.id);
    for (int i = 0; i < m_theme->count(); ++i)
        if (m_theme->itemData(i).toString() == m_result.theme)
            m_theme->setCurrentIndex(i);
    form->addRow(QStringLiteral("Tema:"), m_theme);

    form->addRow(note(QStringLiteral("Skins personalizadas (beta): copia archivos .json en la carpeta de skins y reinicia ReKapture. "
                                     "Dentro hay un archivo LEEME.txt con el formato y un ejemplo."), tab));

    auto *open = new QPushButton(QStringLiteral("Abrir carpeta de skins"), tab);
    connect(open, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::skinsDir()));
    });
    form->addRow(QString(), open);
    return tab;
}

void SettingsDialog::updateNotes()
{
    if (m_encoderNote && m_encoder) {
        const QString key = m_encoder->currentData().toString();
        QString label;
        const QString id = EncoderCatalog::resolve(m_encoders, key, &label);
        m_encoderNote->setText(id.isEmpty() ? QStringLiteral("El codificador elegido no está disponible; se usará el automático.")
                                            : QStringLiteral("Se usará: %1").arg(label));
    }
    if (m_bitrateNote && m_resolution && m_fps && m_quality && m_bitrateMode && m_bitrateSpin) {
        const QSize res = m_resolution->currentData().toSize();
        const bool custom = m_bitrateMode->currentData().toInt() == 1;
        m_quality->setEnabled(!custom);
        m_bitrateSpin->setEnabled(custom);
        const int kbps = custom ? m_bitrateSpin->value()
                                : EncoderCatalog::bitrateKbps(m_quality->currentIndex(), res.width(), res.height(), m_fps->currentData().toInt());
        const double mbPerMinute = (kbps + 2 * 160 + 100) * 1000.0 / 8.0 * 60.0 / 1e6;
        m_bitrateNote->setText(QStringLiteral("Se graba a un bitrate constante (CBR) de %1 kbps (unos %2 MB por minuto). Más bitrate da "
                                              "más calidad y archivos más grandes; el resultado visual puede variar entre codificadores.")
                                   .arg(kbps).arg(static_cast<int>(mbPerMinute) + 1));
        if (m_memoryNote && m_clipSeconds) {
            const double mb = (kbps + 400) * 1000.0 / 8.0 * m_clipSeconds->value() / 1e6;
            m_memoryNote->setText(QStringLiteral("Memoria aproximada del buffer: %1 MB (con la calidad y resolución actuales).")
                                      .arg(static_cast<int>(mb * 1.3) + 1));
        }
    }
}

void SettingsDialog::accept()
{
    auto sameKeys = [](const QKeySequence &a, const QKeySequence &b) { return !a.isEmpty() && a == b; };
    const QList<QKeySequence> keys = {m_startKey->keySequence(), m_stopKey->keySequence(), m_clipKey->keySequence(),
                                      m_replayKey->keySequence()};
    for (int i = 0; i < keys.size(); ++i)
        for (int j = i + 1; j < keys.size(); ++j)
            if (sameKeys(keys[i], keys[j])) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Dos atajos no pueden usar la misma combinación."));
                return;
            }

    const QSize res = m_resolution->currentData().toSize();
    m_result.width = res.width();
    m_result.height = res.height();
    m_result.fps = m_fps->currentData().toInt();
    m_result.encoderKey = m_encoder->currentData().toString();
    m_result.quality = m_quality->currentIndex();
    m_result.bitrateCustom = m_bitrateMode->currentData().toInt() == 1;
    m_result.bitrateKbps = m_bitrateSpin->value();
    m_result.audioBitrateKbps = m_audioBitrate->currentData().toInt();

    m_result.organizeByGame = m_organizeByGame->isChecked();
    m_result.soundCues = m_soundCues->isChecked();
    m_result.overlayEnabled = m_overlayEnabled->isChecked();
    m_result.overlayCorner = m_overlayCorner->currentData().toInt();
    m_result.hideAppFromCapture = m_hideApp->isChecked();

    m_result.micExportMode = m_micExport->currentData().toInt();
    m_result.micFileFormat = m_micFormat->currentData().toInt();
    m_result.webcamResolution = m_webcamRes->currentData().toInt();
    m_result.webcamWithAudio = m_webcamAudio->isChecked();
    m_result.vstEnabled = m_vstEnabled->isChecked();

    m_result.clipsWhileRecording = m_clipsWhileRecording->isChecked();
    m_result.instantReplay = m_instantReplay->isChecked();
    m_result.clipSeconds = m_clipSeconds->value();
    m_result.autoDisableReplay = m_autoDisableReplay->isChecked();

    m_result.startHotkey = keys[0].toString(QKeySequence::PortableText);
    m_result.stopHotkey = keys[1].toString(QKeySequence::PortableText);
    m_result.clipHotkey = keys[2].toString(QKeySequence::PortableText);
    m_result.replayHotkey = keys[3].toString(QKeySequence::PortableText);

    m_result.theme = m_theme->currentData().toString();
    QDialog::accept();
}
