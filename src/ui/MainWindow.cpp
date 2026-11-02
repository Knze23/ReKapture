#include "MainWindow.h"

#include "../audio/SoundCues.h"
#include "../capture/GameNaming.h"
#include "../hotkeys/HotkeyManager.h"
#include "../log/Logger.h"
#include "ActivityOverlay.h"
#include "EffectsDialog.h"
#include "LevelMeter.h"
#include "LibraryPage.h"
#include "LogPage.h"
#include "PreviewWidget.h"
#include "SettingsDialog.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace {



QIcon makeAppIcon()
{
    // El mismo logo que lleva el ejecutable (incluido en los recursos de Qt)
    const QIcon icon(QStringLiteral(":/rekapture.png"));
    if (!icon.isNull())
        return icon;

    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xE5, 0x39, 0x45));
    p.drawEllipse(8, 8, 48, 48);
    return QIcon(pm);
}

QString formatElapsed(qint64 ms)
{
    const qint64 total = ms / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QFrame *makeCard(QWidget *parent, const QString &title, QVBoxLayout **bodyOut)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(16, 14, 16, 16);
    v->setSpacing(8);
    auto *t = new QLabel(title.toUpper(), card);
    t->setObjectName(QStringLiteral("cardTitle"));
    v->addWidget(t);
    *bodyOut = v;
    return card;
}

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ReKapture"));
    setWindowIcon(makeAppIcon());
    setMinimumSize(980, 660);
    resize(1120, 740);

    m_settings = AppSettings::load();
    if (!m_settings.windowGeometry.isEmpty())
        restoreGeometry(QByteArray::fromHex(m_settings.windowGeometry.toLatin1()));

    m_recorder = new RecordingController(this);
    connect(m_recorder, &RecordingController::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_recorder, &RecordingController::recordingStarted, this, &MainWindow::onRecordingStarted);
    connect(m_recorder, &RecordingController::recordingFinished, this, &MainWindow::onRecordingFinished);
    connect(m_recorder, &RecordingController::captureStatus, this, &MainWindow::onCaptureStatus);
    connect(m_recorder, &RecordingController::clipSaved, this, &MainWindow::onClipSaved);
    connect(m_recorder, &RecordingController::replayStateChanged, this, &MainWindow::onReplayStateChanged);
    connect(m_recorder, &RecordingController::performanceWarning, this, &MainWindow::onPerformanceWarning);

    m_hotkeys = new HotkeyManager(this);
    connect(m_hotkeys, &HotkeyManager::triggered, this, &MainWindow::onHotkey);

    buildUi();
    m_overlay = new ActivityOverlay();
    applyTheme();

    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(33);
    connect(m_liveTimer, &QTimer::timeout, this, &MainWindow::updateLive);
    m_liveTimer->start();

    applyHotkeys();
    updateControls();
    updateSummary();
    updateDisk();
    startEngineInit();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("root"));
    auto *main = new QVBoxLayout(root);
    main->setContentsMargins(20, 16, 20, 16);
    main->setSpacing(12);

    // ---------------- Barra superior ----------------
    auto *top = new QHBoxLayout();
    m_logo = new QLabel(root);
    m_logo->setObjectName(QStringLiteral("logo"));
    m_logo->setTextFormat(Qt::RichText);
    m_logo->setText(QStringLiteral("Re<span style='color:#E53945'>Kapture</span>"));

    m_captureChip = new QLabel(QStringLiteral("Iniciando…"), root);
    m_captureChip->setObjectName(QStringLiteral("chip"));
    m_pill = new QLabel(QStringLiteral("LISTO"), root);
    m_pill->setObjectName(QStringLiteral("pill"));
    m_settingsButton = new QPushButton(QStringLiteral("⚙  Ajustes"), root);
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::onOpenSettings);

    top->addWidget(m_logo);
    top->addSpacing(20);

    // Navegacion: Grabar / Biblioteca
    m_navGroup = new QButtonGroup(this);
    auto *nav = new QHBoxLayout();
    nav->setSpacing(0);
    const char *navNames[3] = {"Grabar", "Biblioteca", "Registro"};
    const char *navPos[3] = {"left", "mid", "right"};
    for (int i = 0; i < 3; ++i) {
        auto *b = new QPushButton(QString::fromUtf8(navNames[i]), root);
        b->setObjectName(QStringLiteral("seg"));
        b->setProperty("pos", QString::fromLatin1(navPos[i]));
        b->setCheckable(true);
        b->setMinimumWidth(104);
        m_navGroup->addButton(b, i);
        nav->addWidget(b);
    }
    m_navGroup->button(0)->setChecked(true);
    connect(m_navGroup, &QButtonGroup::idClicked, this, &MainWindow::onNavClicked);
    top->addLayout(nav);
    top->addStretch(1);
    top->addWidget(m_captureChip);
    top->addWidget(m_pill);
    top->addWidget(m_settingsButton);
    main->addLayout(top);

    // ---------------- Cuerpo ----------------
    auto *body = new QHBoxLayout();
    body->setSpacing(14);

    // Columna izquierda: previsualizacion + grabar
    auto *left = new QVBoxLayout();
    left->setSpacing(12);

    auto *previewFrame = new QFrame(root);
    previewFrame->setObjectName(QStringLiteral("previewFrame"));
    auto *previewLayout = new QVBoxLayout(previewFrame);
    previewLayout->setContentsMargins(8, 8, 8, 8);
    m_previewStack = new QStackedWidget(previewFrame);
    m_preview = new PreviewWidget(PreviewWidget::Mode::MainCanvas, m_previewStack);
    m_previewPlaceholder = new QLabel(QStringLiteral("Previsualización desactivada\nahorra recursos del equipo"), m_previewStack);
    m_previewPlaceholder->setObjectName(QStringLiteral("placeholder"));
    m_previewPlaceholder->setAlignment(Qt::AlignCenter);
    m_previewPlaceholder->setWordWrap(true);
    m_previewStack->addWidget(m_preview);
    m_previewStack->addWidget(m_previewPlaceholder);
    previewLayout->addWidget(m_previewStack);
    left->addWidget(previewFrame, 1);

    auto *controls = new QHBoxLayout();
    controls->setSpacing(14);
    m_recButton = new QPushButton(QStringLiteral("●  Iniciar grabación"), root);
    m_recButton->setObjectName(QStringLiteral("record"));
    m_recButton->setMinimumHeight(56);
    m_recButton->setEnabled(false);
    connect(m_recButton, &QPushButton::clicked, this, &MainWindow::onRecordClicked);
    m_timeLabel = new QLabel(QStringLiteral("00:00:00"), root);
    m_timeLabel->setObjectName(QStringLiteral("time"));
    m_previewToggle = new QCheckBox(QStringLiteral("Previsualización"), root);
    m_previewToggle->setChecked(m_settings.previewEnabled);
    connect(m_previewToggle, &QCheckBox::toggled, this, &MainWindow::onPreviewToggled);
    controls->addWidget(m_recButton, 1);
    controls->addWidget(m_timeLabel);
    controls->addWidget(m_previewToggle);
    m_replayToggle = new QCheckBox(QStringLiteral("Repetición"), root);
    m_replayToggle->setChecked(m_settings.instantReplay);
    m_replayToggle->setToolTip(QStringLiteral("Repetición instantánea: mantiene un buffer de los últimos segundos para guardar clips "
                                              "con un atajo. Si notas bajadas de FPS, apágala."));
    connect(m_replayToggle, &QCheckBox::toggled, this, &MainWindow::onReplayToggled);
    controls->addWidget(m_replayToggle);
    left->addLayout(controls);
    body->addLayout(left, 1);

    m_previewStack->setCurrentIndex(m_settings.previewEnabled ? 0 : 1);

    // Columna derecha: tarjetas
    auto *right = new QVBoxLayout();
    right->setSpacing(12);

    // --- Fuente ---
    QVBoxLayout *sourceBody = nullptr;
    right->addWidget(makeCard(root, QStringLiteral("Fuente de video"), &sourceBody));

    auto *segRow = new QHBoxLayout();
    segRow->setSpacing(0);
    m_modeGroup = new QButtonGroup(this);
    const char *segNames[3] = {"Monitor", "Ventana", "Juego"};
    const char *segTips[3] = {"Graba una pantalla completa. Es el modo más compatible.",
                              "Graba una sola ventana (captura de ventana de OBS).",
                              "Usa la captura de juego de OBS. Con juegos con anticheat puede estar bloqueada "
                              "y no es recomendable: usa Monitor o Ventana."};
    const char *segPos[3] = {"left", "mid", "right"};
    for (int i = 0; i < 3; ++i) {
        auto *b = new QPushButton(QString::fromUtf8(segNames[i]), root);
        b->setObjectName(QStringLiteral("seg"));
        b->setProperty("pos", QString::fromLatin1(segPos[i]));
        b->setCheckable(true);
        b->setToolTip(QString::fromUtf8(segTips[i]));
        b->setEnabled(false);
        m_modeGroup->addButton(b, i);
        segRow->addWidget(b, 1);
    }
    if (auto *b = m_modeGroup->button(m_settings.captureMode))
        b->setChecked(true);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, &MainWindow::onModeClicked);
    sourceBody->addLayout(segRow);

    m_targetLabel = new QLabel(QStringLiteral("Monitor a grabar:"), root);
    m_targetLabel->setObjectName(QStringLiteral("muted"));
    sourceBody->addWidget(m_targetLabel);
    auto *targetRow = new QHBoxLayout();
    m_targetCombo = new QComboBox(root);
    m_targetCombo->setEnabled(false);
    m_refreshButton = new QPushButton(QStringLiteral("↻"), root);
    m_refreshButton->setToolTip(QStringLiteral("Actualizar la lista de ventanas"));
    m_refreshButton->setFixedWidth(40);
    m_refreshButton->setEnabled(false);
    targetRow->addWidget(m_targetCombo, 1);
    targetRow->addWidget(m_refreshButton);
    sourceBody->addLayout(targetRow);
    connect(m_targetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onTargetChanged);
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshTargets);

    // --- Audio ---
    QVBoxLayout *audioBody = nullptr;
    right->addWidget(makeCard(root, QStringLiteral("Audio"), &audioBody));

    auto addAudioRow = [&](const QString &title, QComboBox *&combo, QPushButton *&fx, LevelMeter *&meter, QSlider *&slider, QLabel *&volLabel) {
        auto *label = new QLabel(title, root);
        label->setObjectName(QStringLiteral("muted"));
        audioBody->addWidget(label);
        combo = new QComboBox(root);
        combo->setEnabled(false);
        fx = new QPushButton(QStringLiteral("Efectos"), root);
        fx->setToolTip(QStringLiteral("Supresor de ruido, compresor, ecualizador y plugins VST"));
        fx->setEnabled(false);
        auto *deviceRow = new QHBoxLayout();
        deviceRow->addWidget(combo, 1);
        deviceRow->addWidget(fx);
        audioBody->addLayout(deviceRow);
        meter = new LevelMeter(root);
        audioBody->addWidget(meter);
        auto *row = new QHBoxLayout();
        slider = new QSlider(Qt::Horizontal, root);
        slider->setRange(0, 150);
        volLabel = new QLabel(QStringLiteral("100%"), root);
        volLabel->setObjectName(QStringLiteral("muted"));
        volLabel->setMinimumWidth(40);
        volLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(slider, 1);
        row->addWidget(volLabel);
        audioBody->addLayout(row);
    };
    addAudioRow(QStringLiteral("Escritorio · archivo principal"), m_desktopCombo, m_desktopFx, m_desktopMeter, m_desktopSlider, m_desktopVolLabel);
    audioBody->addSpacing(4);
    addAudioRow(QStringLiteral("Micrófono"), m_micCombo, m_micFx, m_micMeter, m_micSlider, m_micVolLabel);
    connect(m_desktopFx, &QPushButton::clicked, this, [this] { openEffects(0); });
    connect(m_micFx, &QPushButton::clicked, this, [this] { openEffects(1); });

    m_desktopSlider->setValue(m_settings.desktopVolume);
    m_micSlider->setValue(m_settings.micVolume);
    m_desktopVolLabel->setText(QStringLiteral("%1%").arg(m_settings.desktopVolume));
    m_micVolLabel->setText(QStringLiteral("%1%").arg(m_settings.micVolume));
    connect(m_desktopCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onDeviceChanged);
    connect(m_micCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onDeviceChanged);
    connect(m_desktopSlider, &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
    connect(m_micSlider, &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
    connect(m_desktopSlider, &QSlider::sliderReleased, this, &MainWindow::onVolumeReleased);
    connect(m_micSlider, &QSlider::sliderReleased, this, &MainWindow::onVolumeReleased);

    // --- Webcam (se graba en un MP4 independiente) ---
    QVBoxLayout *camBody = nullptr;
    right->addWidget(makeCard(root, QStringLiteral("Webcam"), &camBody));
    m_webcamToggle = new QCheckBox(QStringLiteral("Grabar la webcam en un archivo aparte"), root);
    m_webcamToggle->setChecked(m_settings.webcamEnabled);
    camBody->addWidget(m_webcamToggle);
    auto *camRow = new QHBoxLayout();
    m_webcamCombo = new QComboBox(root);
    m_webcamCombo->setEnabled(false);
    m_webcamRefresh = new QPushButton(QStringLiteral("↻"), root);
    m_webcamRefresh->setToolTip(QStringLiteral("Actualizar la lista de cámaras"));
    m_webcamRefresh->setFixedWidth(40);
    m_webcamRefresh->setEnabled(false);
    camRow->addWidget(m_webcamCombo, 1);
    camRow->addWidget(m_webcamRefresh);
    camBody->addLayout(camRow);
    auto *camNote = new QLabel(QStringLiteral("Nunca se mezcla con el vídeo de la pantalla: sale como …_CAM.mp4."), root);
    camNote->setObjectName(QStringLiteral("muted"));
    camNote->setWordWrap(true);
    camBody->addWidget(camNote);
    m_webcamPreviewToggle = new QCheckBox(QStringLiteral("Vista previa de la webcam"), root);
    m_webcamPreviewToggle->setChecked(m_settings.webcamPreview);
    m_webcamPreviewToggle->setToolTip(QStringLiteral("Abre la cámara para verla aquí. Al desactivarla (y sin grabar la webcam) la cámara se cierra."));
    camBody->addWidget(m_webcamPreviewToggle);
    m_camStack = new QStackedWidget(root);
    m_camStack->setFixedHeight(190);
    m_camPreview = new PreviewWidget(PreviewWidget::Mode::Source, m_camStack);
    m_camPlaceholder = new QLabel(QStringLiteral("La vista previa de la webcam\nestá desactivada."), m_camStack);
    m_camPlaceholder->setObjectName(QStringLiteral("placeholder"));
    m_camPlaceholder->setAlignment(Qt::AlignCenter);
    m_camStack->addWidget(m_camPreview);
    m_camStack->addWidget(m_camPlaceholder);
    m_camStack->setCurrentIndex(m_settings.webcamPreview ? 0 : 1);
    camBody->addWidget(m_camStack);
    connect(m_webcamPreviewToggle, &QCheckBox::toggled, this, &MainWindow::onWebcamPreviewToggled);
    connect(m_webcamToggle, &QCheckBox::toggled, this, &MainWindow::onWebcamToggled);
    connect(m_webcamCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onWebcamChanged);
    connect(m_webcamRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshWebcams);

    // --- Salida ---
    QVBoxLayout *outBody = nullptr;
    right->addWidget(makeCard(root, QStringLiteral("Salida"), &outBody));
    m_folderLabel = new QLabel(root);
    m_folderLabel->setWordWrap(true);
    m_folderLabel->setText(QDir::toNativeSeparators(m_settings.folder));
    outBody->addWidget(m_folderLabel);
    auto *folderRow = new QHBoxLayout();
    m_folderButton = new QPushButton(QStringLiteral("Cambiar…"), root);
    m_openFolderButton = new QPushButton(QStringLiteral("Abrir carpeta"), root);
    connect(m_folderButton, &QPushButton::clicked, this, &MainWindow::onChooseFolder);
    connect(m_openFolderButton, &QPushButton::clicked, this, &MainWindow::onOpenFolder);
    folderRow->addWidget(m_folderButton);
    folderRow->addWidget(m_openFolderButton);
    outBody->addLayout(folderRow);
    m_summaryLabel = new QLabel(root);
    m_summaryLabel->setObjectName(QStringLiteral("muted"));
    m_summaryLabel->setWordWrap(true);
    m_diskLabel = new QLabel(root);
    m_diskLabel->setObjectName(QStringLiteral("muted"));
    m_diskLabel->setWordWrap(true);
    outBody->addWidget(m_summaryLabel);
    outBody->addWidget(m_diskLabel);

    right->addStretch(1);
    auto *rightWidget = new QWidget();
    rightWidget->setLayout(right);
    right->setContentsMargins(0, 0, 8, 0);
    auto *rightScroll = new QScrollArea(root);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScroll->setFixedWidth(384);
    rightScroll->setWidget(rightWidget);
    body->addWidget(rightScroll);
    m_pages = new QStackedWidget(root);
    auto *recordPage = new QWidget(m_pages);
    recordPage->setLayout(body);
    body->setContentsMargins(0, 0, 0, 0);
    m_pages->addWidget(recordPage);
    m_library = new LibraryPage(m_pages);
    m_library->setFolder(m_settings.folder);
    m_pages->addWidget(m_library);
    m_logPage = new LogPage(m_pages);
    m_pages->addWidget(m_logPage);
    main->addWidget(m_pages, 1);

    // ---------------- Barra de estado ----------------
    auto *bottom = new QHBoxLayout();
    m_status = new QLabel(QStringLiteral("Inicializando el motor…"), root);
    m_status->setObjectName(QStringLiteral("statusBar"));
    m_status->setWordWrap(false);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); // una sola linea que nunca agranda la ventana
    m_status->setMinimumHeight(40);
    m_status->setCursor(Qt::PointingHandCursor);
    m_status->installEventFilter(this); // un clic abre la pestana Registro
    m_status->setToolTip(QStringLiteral("Aquí aparece el último aviso. El detalle completo está en la pestaña Registro."));
    bottom->addWidget(m_status, 1);
    main->addLayout(bottom);

    setCentralWidget(root);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_affinitySet) {
        qApp->installEventFilter(this); // aplica la visibilidad tambien a los dialogos nuevos
        m_affinitySet = true;
    }
    applyCaptureVisibility();
}

// Por defecto la ventana de ReKapture SI sale en las grabaciones y capturas. Si el usuario lo pide,
// se excluye (esto no afecta al icono de actividad, que siempre se excluye por su cuenta).
void MainWindow::applyCaptureVisibility()
{
    const DWORD affinity = m_settings.hideAppFromCapture ? WDA_EXCLUDEFROMCAPTURE : 0 /* WDA_NONE */;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (w == m_overlay || !w->isWindow() || !w->testAttribute(Qt::WA_WState_Created))
            continue;
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(w->winId()), affinity);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_status && event->type() == QEvent::MouseButtonRelease) {
        if (QAbstractButton *log = m_navGroup->button(2))
            log->click();
        return false;
    }

    if (event->type() == QEvent::Show && m_settings.hideAppFromCapture) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w && w->isWindow() && w != m_overlay && w->testAttribute(Qt::WA_WState_Created))
            SetWindowDisplayAffinity(reinterpret_cast<HWND>(w->winId()), WDA_EXCLUDEFROMCAPTURE);
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::applyHotkeys()
{
    m_hotkeys->unregisterAll();
    QStringList warnings;
    QString error;

    if (!m_hotkeys->registerHotkey(HotkeyManager::Start,
                                   QKeySequence::fromString(m_settings.startHotkey, QKeySequence::PortableText), &error))
        warnings << QStringLiteral("Atajo de iniciar: %1").arg(error);
    if (!m_hotkeys->registerHotkey(HotkeyManager::Stop,
                                   QKeySequence::fromString(m_settings.stopHotkey, QKeySequence::PortableText), &error))
        warnings << QStringLiteral("Atajo de detener: %1").arg(error);

    if (!m_hotkeys->registerHotkey(HotkeyManager::SaveClip,
                                   QKeySequence::fromString(m_settings.clipHotkey, QKeySequence::PortableText), &error))
        warnings << QStringLiteral("Atajo de clip: %1").arg(error);
    if (!m_hotkeys->registerHotkey(HotkeyManager::ToggleReplay,
                                   QKeySequence::fromString(m_settings.replayHotkey, QKeySequence::PortableText), &error))
        warnings << QStringLiteral("Atajo de repetición: %1").arg(error);

    m_hotkeyWarning = warnings.join(QStringLiteral("\n"));
    for (const QString &w : warnings)
        Logger::warn(w);
}

void MainWindow::startEngineInit()
{
    // La inicializacion se hace en un hilo aparte para no congelar la ventana.
    auto result = std::make_shared<ObsInitResult>();
    const AppSettingsData s = m_settings;
    m_initThread = QThread::create([this, result, s] { *result = m_engine.initialize(s.width, s.height, s.fps, s.vstEnabled); });

    connect(m_initThread, &QThread::finished, this, [this, result] {
        if (m_closing)
            return;
        if (m_initThread) {
            m_initThread->deleteLater();
            m_initThread = nullptr;
        }
        showInitResult(*result);
    });
    m_initThread->start();
}

void MainWindow::selectById(QComboBox *combo, const QString &id)
{
    const int index = combo->findData(id);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

void MainWindow::showInitResult(const ObsInitResult &r)
{
    if (!r.ok) {
        setStatus(QStringLiteral("Error al inicializar el motor: %1\nConsulta el registro: %2")
                      .arg(r.errorMessage, Logger::logFilePath()), true);
        m_captureChip->setText(QStringLiteral("Error del motor"));
        return;
    }

    m_engineReady = true;
    m_encoders = EncoderCatalog::detect(r.encoders);

    // Dispositivos de audio (el primero de cada lista es el predeterminado de Windows)
    {
        QSignalBlocker b1(m_desktopCombo);
        QSignalBlocker b2(m_micCombo);
        m_desktopCombo->clear();
        for (const DeviceInfo &d : m_recorder->listAudioDevices(false))
            m_desktopCombo->addItem(d.name, d.id);
        m_desktopCombo->addItem(QStringLiteral("(Sin audio del escritorio)"), QString());
        selectById(m_desktopCombo, m_settings.desktopDeviceId);

        m_micCombo->clear();
        for (const DeviceInfo &d : m_recorder->listAudioDevices(true))
            m_micCombo->addItem(d.name, d.id);
        m_micCombo->addItem(QStringLiteral("(Sin micrófono)"), QString());
        selectById(m_micCombo, m_settings.micDeviceId);
    }
    m_settings.desktopDeviceId = m_desktopCombo->currentData().toString();
    m_settings.micDeviceId = m_micCombo->currentData().toString();

    populateTargets();
    populateWebcams();

    // Efectos guardados (se aplican cuando se crean las fuentes de audio)
    m_recorder->setEffectsJson(0, m_settings.desktopEffects);
    m_recorder->setEffectsJson(1, m_settings.micEffects);

    m_preview->ensureDisplay();
    m_camPreview->ensureDisplay();
    updatePreviewActivity();

    QString status = QStringLiteral("Motor listo (OBS %1).").arg(r.version);
    if (m_targetCombo->count() == 0)
        status = QStringLiteral("No hay ningún objetivo de captura disponible.");
    if (!m_hotkeyWarning.isEmpty())
        status += QStringLiteral("\n") + m_hotkeyWarning;
    else
        status += QStringLiteral(" Atajos: %1 para iniciar, %2 para detener.")
                      .arg(QKeySequence::fromString(m_settings.startHotkey, QKeySequence::PortableText).toString(QKeySequence::NativeText),
                           QKeySequence::fromString(m_settings.stopHotkey, QKeySequence::PortableText).toString(QKeySequence::NativeText));
    if (!r.skippedModules.isEmpty())
        status += QStringLiteral("\nSe desactivó el módulo %1 porque cerró la aplicación la última vez. "
                                 "Puedes reintentarlo en Ajustes > Audio y webcam.")
                      .arg(r.skippedModules.join(QStringLiteral(", ")));
    setStatus(status, m_targetCombo->count() == 0 || !m_hotkeyWarning.isEmpty() || !r.skippedModules.isEmpty());

    updateControls();
    updateSummary();
    refreshIdleState();
}

void MainWindow::setStatus(const QString &text, bool error)
{
    // Una sola linea; el texto completo queda en el tooltip y lo importante en la pestaña Registro
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString oneLine = lines.isEmpty() ? QString() : lines.first().trimmed();
    if (lines.size() > 1 && oneLine.endsWith(QLatin1Char(':')))
        oneLine += QStringLiteral(" ") + lines.at(1).trimmed(); // "Guardado:" + ruta
    if (lines.size() > 2)
        oneLine += QStringLiteral("  (+%1)").arg(lines.size() - 2);
    m_status->setText(oneLine);
    m_status->setToolTip(text.trimmed());
    m_status->setProperty("error", error);
    repolish(m_status);
}

RecordingOptions MainWindow::buildOptions() const
{
    RecordingOptions opt;
    opt.captureMode = static_cast<CaptureMode>(m_settings.captureMode);
    opt.monitorId = m_settings.monitorId;
    opt.windowId = m_settings.windowId;
    opt.folder = m_settings.folder;

    opt.desktopDeviceId = m_settings.desktopDeviceId;
    opt.recordDesktopAudio = !opt.desktopDeviceId.isEmpty();
    opt.desktopVolumePct = m_settings.desktopVolume;
    opt.micDeviceId = m_settings.micDeviceId;
    opt.recordMic = !opt.micDeviceId.isEmpty();
    opt.micVolumePct = m_settings.micVolume;
    opt.micExportMode = m_settings.micExportMode;
    opt.micFileFormat = m_settings.micFileFormat;

    opt.webcamEnabled = m_settings.webcamEnabled && !m_settings.webcamDeviceId.isEmpty();
    opt.webcamDeviceId = m_settings.webcamDeviceId;
    opt.webcamWidth = m_settings.webcamResolution == 1 ? 1920 : 1280;
    opt.webcamHeight = m_settings.webcamResolution == 1 ? 1080 : 720;
    opt.webcamWithAudio = m_settings.webcamWithAudio;

    QString label;
    QString encoderId = EncoderCatalog::resolve(m_encoders, m_settings.encoderKey, &label);
    if (encoderId.isEmpty())
        encoderId = EncoderCatalog::resolve(m_encoders, QStringLiteral("auto"), &label);
    opt.videoEncoderId = encoderId;
    opt.width = m_settings.width;
    opt.height = m_settings.height;
    opt.fps = m_settings.fps;
    opt.gameName = currentGameName();
    opt.organizeByGame = m_settings.organizeByGame;
    opt.instantReplay = m_settings.instantReplay;
    opt.clipsWhileRecording = m_settings.clipsWhileRecording;
    opt.clipSeconds = m_settings.clipSeconds;
    opt.bitrateKbps = videoBitrateKbps();
    opt.audioBitrateKbps = m_settings.audioBitrateKbps;
    opt.webcamPreview = m_settings.webcamPreview && !m_settings.webcamDeviceId.isEmpty();
    return opt;
}

void MainWindow::refreshIdleState()
{
    if (!m_engineReady || m_recorder->state() != RecordingController::State::Idle)
        return;
    // La captura queda armada con la previsualizacion activa o en modo juego (para que el juego
    // ya este enganchado cuando se pulsa grabar y no salgan segundos en negro).
    const bool captureVideo = m_settings.previewEnabled || m_settings.captureMode == 2 || m_settings.instantReplay;
    m_recorder->applyIdleState(buildOptions(), captureVideo);
    syncCameraPreview();
    updateCaptureChip();
}

void MainWindow::updateCaptureChip()
{
    QString text;
    QString state;
    if (!m_engineReady) {
        text = QStringLiteral("Iniciando…");
    } else if (m_settings.captureMode == 2) {
        if (m_recorder->gameHooked()) {
            text = m_hookText.isEmpty() ? QStringLiteral("Juego capturado") : m_hookText;
            state = QStringLiteral("ok");
        } else {
            text = QStringLiteral("Esperando al juego…");
            state = QStringLiteral("wait");
        }
    } else if (m_settings.previewEnabled) {
        text = QStringLiteral("Captura lista");
        state = QStringLiteral("ok");
    } else {
        text = QStringLiteral("Captura en reposo");
    }
    m_captureChip->setText(text);
    m_captureChip->setProperty("state", state);
    repolish(m_captureChip);
}

void MainWindow::updateControls()
{
    const auto state = m_recorder->state();
    const bool idle = state == RecordingController::State::Idle;
    const int mode = m_settings.captureMode;

    for (QAbstractButton *b : m_modeGroup->buttons())
        b->setEnabled(m_engineReady && idle);
    m_refreshButton->setEnabled(m_engineReady && idle && mode != 0);
    m_targetCombo->setEnabled(m_engineReady && idle && m_targetCombo->count() > 0);
    m_desktopCombo->setEnabled(m_engineReady && idle && m_desktopCombo->count() > 0);
    m_micCombo->setEnabled(m_engineReady && idle && m_micCombo->count() > 0);
    m_desktopFx->setEnabled(m_engineReady && m_recorder->audioSourceExists(0));
    m_micFx->setEnabled(m_engineReady && m_recorder->audioSourceExists(1));
    m_webcamToggle->setEnabled(m_engineReady && idle);
    m_webcamCombo->setEnabled(m_engineReady && idle && m_webcamCombo->count() > 0);
    m_webcamRefresh->setEnabled(m_engineReady && idle);
    m_folderButton->setEnabled(idle);
    m_settingsButton->setEnabled(m_engineReady && idle);

    QString pillText = QStringLiteral("LISTO");
    QString pillState;
    bool recording = false;

    switch (state) {
    case RecordingController::State::Idle:
        m_recButton->setText(QStringLiteral("●  Iniciar grabación"));
        m_recButton->setEnabled(m_engineReady && m_targetCombo->count() > 0);
        break;
    case RecordingController::State::Starting:
        m_recButton->setText(QStringLiteral("Iniciando…"));
        m_recButton->setEnabled(false);
        pillText = QStringLiteral("INICIANDO");
        pillState = QStringLiteral("busy");
        break;
    case RecordingController::State::Recording:
        m_recButton->setText(QStringLiteral("■  Detener grabación"));
        m_recButton->setEnabled(true);
        pillText = QStringLiteral("● REC");
        pillState = QStringLiteral("rec");
        recording = true;
        break;
    case RecordingController::State::Stopping:
        m_recButton->setText(QStringLiteral("Guardando archivos…"));
        m_recButton->setEnabled(false);
        pillText = QStringLiteral("GUARDANDO");
        pillState = QStringLiteral("busy");
        break;
    }

    updateOverlay();
    updatePreviewActivity();
    m_pill->setText(pillText);
    m_pill->setProperty("state", pillState);
    repolish(m_pill);
    m_recButton->setProperty("recording", recording);
    repolish(m_recButton);
}

void MainWindow::updateSummary()
{
    QString label;
    QString id = EncoderCatalog::resolve(m_encoders, m_settings.encoderKey, &label);
    if (id.isEmpty())
        id = EncoderCatalog::resolve(m_encoders, QStringLiteral("auto"), &label);
    if (label.isEmpty())
        label = QStringLiteral("codificador automático");

    const int kbps = videoBitrateKbps();
    const QString quality = m_settings.bitrateCustom
                                ? QStringLiteral("Bitrate personalizado: %1 kbps").arg(kbps)
                                : QStringLiteral("Calidad %1 (≈ %2 kbps)").arg(EncoderCatalog::qualityLabels().value(m_settings.quality)).arg(kbps);
    m_summaryLabel->setText(QStringLiteral("%1×%2 · %3 FPS · %4\n%5")
                                .arg(m_settings.width).arg(m_settings.height).arg(m_settings.fps).arg(label, quality));

    QString mic;
    if (m_settings.micDeviceId.isEmpty()) {
        mic = QStringLiteral("sin micrófono");
    } else {
        const QString fmt = m_settings.micFileFormat == 0 ? QStringLiteral("WAV")
                            : m_settings.micFileFormat == 1 ? QStringLiteral("MP3") : QStringLiteral("MP4");
        mic = m_settings.micExportMode == 0 ? QStringLiteral("%1 aparte").arg(fmt)
              : m_settings.micExportMode == 1 ? QStringLiteral("multipista en el vídeo")
                                              : QStringLiteral("multipista + %1 aparte").arg(fmt);
    }
    m_summaryLabel->setText(m_summaryLabel->text() + QStringLiteral("\nMicrófono: %1%2").arg(mic,
                            m_settings.webcamEnabled ? QStringLiteral(" · Webcam: MP4 aparte") : QString()));
}

void MainWindow::updateDisk()
{
    QString folder = m_settings.folder;
    if (!QDir(folder).exists())
        folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QStorageInfo storage(folder);
    if (!storage.isValid()) {
        m_diskLabel->setText(QString());
        return;
    }
    const qint64 free = storage.bytesAvailable();
    // Estimacion: video + dos pistas de audio + archivo del micrófono
    const int kbps = videoBitrateKbps() + 2 * m_settings.audioBitrateKbps + 100;
    const qint64 bytesPerSecond = static_cast<qint64>(kbps) * 1000 / 8;
    const qint64 seconds = bytesPerSecond > 0 ? free / bytesPerSecond : 0;
    m_diskLabel->setText(QStringLiteral("Espacio libre: %1 · ≈ %2 h %3 min de grabación")
                             .arg(QLocale().formattedDataSize(free, 1))
                             .arg(seconds / 3600)
                             .arg((seconds / 60) % 60));
}

void MainWindow::updateLive()
{
    // Los medidores solo se animan cuando se ven (menos trabajo en la biblioteca, el registro o minimizada)
    if (m_pages->currentIndex() == 0 && !isMinimized()) {
        m_desktopMeter->setLevel(m_recorder->desktopLevel());
        m_micMeter->setLevel(m_recorder->micLevel());
    }

    if (m_recorder->state() == RecordingController::State::Recording)
        m_timeLabel->setText(formatElapsed(m_elapsed.elapsed()));

    ++m_liveTick;
    if (m_liveTick % 30 == 0)
        refreshForeground(); // ~1 s
    if (m_liveTick >= 60) { // cada ~2 s
        m_liveTick = 0;
        updateDisk();
    }
}

void MainWindow::rememberTarget(int mode)
{
    if (m_targetCombo->count() == 0)
        return;
    if (mode == 0)
        m_settings.monitorId = m_targetCombo->currentData().toString();
    else
        m_settings.windowId = m_targetCombo->currentData().toString();
}

void MainWindow::populateTargets()
{
    const int mode = m_settings.captureMode;
    {
        QSignalBlocker blocker(m_targetCombo);
        m_targetCombo->clear();

        if (mode == 0) {
            m_targetLabel->setText(QStringLiteral("Monitor a grabar"));
            for (const MonitorInfo &m : m_recorder->listMonitors())
                m_targetCombo->addItem(m.name, m.id);
            selectById(m_targetCombo, m_settings.monitorId); // si ya no existe, queda el primero
        } else {
            m_targetLabel->setText(mode == 1 ? QStringLiteral("Ventana a grabar") : QStringLiteral("Juego a grabar"));
            if (mode == 2)
                m_targetCombo->addItem(QStringLiteral("Detectar el juego automáticamente (pantalla completa)"), QString());
            for (const MonitorInfo &w : m_recorder->listWindows())
                m_targetCombo->addItem(w.name, w.id);
            selectById(m_targetCombo, m_settings.windowId);
        }
    }
    // Lo que se ve en la lista es lo que se usara
    if (m_targetCombo->count() > 0) {
        if (mode == 0)
            m_settings.monitorId = m_targetCombo->currentData().toString();
        else
            m_settings.windowId = m_targetCombo->currentData().toString();
    }
    updateControls();
}

void MainWindow::onModeClicked(int mode)
{
    if (!m_engineReady || m_recorder->state() != RecordingController::State::Idle)
        return;
    rememberTarget(m_settings.captureMode); // objetivo del modo anterior
    m_settings.captureMode = mode;
    m_hookText.clear();
    populateTargets();
    AppSettings::save(m_settings);
    updatePreviewActivity();
    refreshIdleState();
}

void MainWindow::onTargetChanged()
{
    if (!m_engineReady)
        return;
    rememberTarget(m_settings.captureMode);
    AppSettings::save(m_settings);
    m_hookText.clear();
    refreshIdleState();
}

void MainWindow::onRefreshTargets()
{
    rememberTarget(m_settings.captureMode);
    populateTargets();
    refreshIdleState();
}

void MainWindow::onDeviceChanged()
{
    if (!m_engineReady)
        return;
    m_settings.desktopDeviceId = m_desktopCombo->currentData().toString();
    m_settings.micDeviceId = m_micCombo->currentData().toString();
    AppSettings::save(m_settings);
    refreshIdleState();
}

void MainWindow::onVolumeChanged()
{
    m_settings.desktopVolume = m_desktopSlider->value();
    m_settings.micVolume = m_micSlider->value();
    m_desktopVolLabel->setText(QStringLiteral("%1%").arg(m_settings.desktopVolume));
    m_micVolLabel->setText(QStringLiteral("%1%").arg(m_settings.micVolume));
    if (m_engineReady)
        m_recorder->setVolumes(m_settings.desktopVolume, m_settings.micVolume);
}

void MainWindow::onVolumeReleased()
{
    AppSettings::save(m_settings);
}

void MainWindow::onPreviewToggled(bool on)
{
    m_settings.previewEnabled = on;
    AppSettings::save(m_settings);
    updatePreviewActivity();
    refreshIdleState();
}

void MainWindow::startRecording(bool interactive)
{
    if (!m_engineReady || m_recorder->state() != RecordingController::State::Idle)
        return;

    refreshForeground(); // al usar el atajo, la ventana en primer plano es el juego
    RecordingOptions opt = buildOptions();
    if (opt.videoEncoderId.isEmpty()) {
        const QString msg = QStringLiteral("No hay ningún codificador de video H.264 disponible.");
        setStatus(msg, true);
        QMessageBox::warning(this, QStringLiteral("ReKapture"), msg);
        return;
    }

    // Resolucion y FPS (solo se pueden cambiar sin grabaciones activas)
    QString videoError;
    if (!m_engine.applyVideo(m_settings.width, m_settings.height, m_settings.fps, &videoError)) {
        setStatus(videoError, true);
        QMessageBox::warning(this, QStringLiteral("ReKapture"), videoError);
        return;
    }

    // Juego aun sin enganchar: los primeros segundos saldrian en negro
    if (interactive && m_settings.captureMode == 2 && !m_recorder->gameHooked()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("ReKapture"),
            QStringLiteral("El juego todavía no está capturado: los primeros segundos de la grabación saldrían en negro.\n"
                           "¿Quieres grabar de todas formas?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    // Dispositivos ausentes: avisar y dejar que el usuario decida si continua.
    auto hasRealDevice = [this](bool input) {
        for (const DeviceInfo &d : m_recorder->listAudioDevices(input))
            if (d.id != QStringLiteral("default"))
                return true;
        return false;
    };
    if (opt.recordMic && opt.micDeviceId == QStringLiteral("default") && !hasRealDevice(true)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("ReKapture"),
            QStringLiteral("No se detectó ningún micrófono.\n¿Quieres grabar solo el audio del escritorio, sin el archivo _MIC?"));
        if (answer != QMessageBox::Yes)
            return;
        opt.recordMic = false;
    }
    if (opt.recordDesktopAudio && opt.desktopDeviceId == QStringLiteral("default") && !hasRealDevice(false)) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("ReKapture"),
            QStringLiteral("No se detectó ningún dispositivo de audio del escritorio.\n¿Quieres grabar sin audio del escritorio?"));
        if (answer != QMessageBox::Yes)
            return;
        opt.recordDesktopAudio = false;
    }

    // Webcam: si la camara guardada ya no esta conectada, avisar y dejar decidir
    if (m_settings.webcamEnabled) {
        bool found = false;
        for (const DeviceInfo &d : m_recorder->listWebcams())
            if (d.id == m_settings.webcamDeviceId)
                found = true;
        if (!found) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("ReKapture"),
                QStringLiteral("No se encuentra la webcam elegida (¿está desconectada?).\n¿Quieres grabar sin la webcam?"));
            if (answer != QMessageBox::Yes)
                return;
            opt.webcamEnabled = false;
        }
    }

    AppSettings::save(m_settings);
    m_recordingMic = opt.recordMic;
    m_recordingCam = opt.webcamEnabled;

    // Aviso de inicio: suena ANTES de capturar, asi que no queda en el audio grabado.
    if (m_settings.soundCues)
        SoundCues::play(SoundCues::Cue::Start, true);

    QString error;
    if (!m_recorder->start(opt, &error)) {
        if (m_settings.soundCues)
            SoundCues::play(SoundCues::Cue::Error, false);
        setStatus(error, true);
        QMessageBox::warning(this, QStringLiteral("ReKapture"), error);
        refreshIdleState();
    }
}

void MainWindow::stopRecording()
{
    if (m_recorder->state() == RecordingController::State::Recording)
        m_recorder->stop();
}

void MainWindow::onRecordClicked()
{
    switch (m_recorder->state()) {
    case RecordingController::State::Idle:
        startRecording(true);
        break;
    case RecordingController::State::Recording:
        stopRecording();
        break;
    default:
        break; // ignoramos clics durante las transiciones
    }
}

void MainWindow::onHotkey(int id)
{
    static const char *names[] = {"", "iniciar", "detener", "guardar clip", "alternar repetición"};
    Logger::info(QStringLiteral("Atajo global activado: %1").arg(QString::fromUtf8(names[qBound(0, id, 4)])));
    if (id == HotkeyManager::Start)
        startRecording(false);
    else if (id == HotkeyManager::Stop)
        stopRecording();
    else if (id == HotkeyManager::SaveClip)
        saveClip();
    else if (id == HotkeyManager::ToggleReplay)
        toggleReplay();
}

void MainWindow::onChooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Carpeta de grabaciones"), m_settings.folder);
    if (dir.isEmpty())
        return;
    m_settings.folder = dir;
    m_folderLabel->setText(QDir::toNativeSeparators(m_settings.folder));
    AppSettings::save(m_settings);
    m_library->setFolder(m_settings.folder);
    updateDisk();
}

void MainWindow::onOpenFolder()
{
    QDir().mkpath(m_settings.folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.folder));
}

void MainWindow::onOpenSettings()
{
    const bool previousVst = m_settings.vstEnabled;
    SettingsDialog dialog(m_settings, m_encoders, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_settings = dialog.result();
    AppSettings::save(m_settings);
    const bool vstChanged = m_settings.vstEnabled != previousVst;
    applyHotkeys();
    applyTheme();
    applyCaptureVisibility();
    {
        QSignalBlocker blocker(m_replayToggle);
        m_replayToggle->setChecked(m_settings.instantReplay);
    }

    QString videoError;
    if (!m_engine.applyVideo(m_settings.width, m_settings.height, m_settings.fps, &videoError))
        QMessageBox::warning(this, QStringLiteral("ReKapture"), videoError);

    updateSummary();
    updateDisk();
    refreshIdleState();

    QString status = QStringLiteral("Ajustes guardados.");
    if (vstChanged)
        status += QStringLiteral(" Reinicia ReKapture para %1 los plugins VST.")
                      .arg(m_settings.vstEnabled ? QStringLiteral("activar") : QStringLiteral("desactivar"));
    if (!m_hotkeyWarning.isEmpty())
        status += QStringLiteral("\n") + m_hotkeyWarning;
    setStatus(status, !m_hotkeyWarning.isEmpty());
}

void MainWindow::onStateChanged(int)
{
    updateControls();
}

void MainWindow::onRecordingStarted(const QStringList &files)
{
    m_elapsed.restart();
    QStringList names;
    for (const QString &f : files)
        names << QDir::toNativeSeparators(f);
    m_filesText = names.join(QStringLiteral("\n"));
    setStatus(QStringLiteral("Grabando en:\n%1").arg(m_filesText));
}

void MainWindow::onRecordingFinished(const QStringList &files, bool ok, const QString &message)
{
    QStringList names;
    for (const QString &f : files)
        names << QDir::toNativeSeparators(f);

    if (m_settings.soundCues)
        SoundCues::play(ok ? SoundCues::Cue::Stop : SoundCues::Cue::Error, false);

    if (ok) {
        setStatus(QStringLiteral("Guardado:\n%1").arg(names.join(QStringLiteral("\n"))));
    } else {
        setStatus(QStringLiteral("La grabación terminó con problemas:\n%1").arg(message), true);
        QMessageBox::warning(this, QStringLiteral("ReKapture"), message);
    }
    updateControls();
    updateDisk();
    m_library->markDirty();
    refreshIdleState(); // vuelve a dejar armadas las fuentes

    if (m_closeAfterStop)
        close();
}

void MainWindow::onCaptureStatus(const QString &text)
{
    if (text.startsWith(QStringLiteral("Capturando juego")))
        m_hookText = text;
    else if (text.startsWith(QStringLiteral("Se perdió")))
        m_hookText.clear();
    updateCaptureChip();

    if (m_recorder->state() == RecordingController::State::Recording && !text.startsWith(QStringLiteral("Capturando juego")))
        setStatus(text + QStringLiteral("\n\n") + m_filesText, true);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // No se cierra mientras se esta grabando o finalizando un archivo.
    if (m_recorder->state() != RecordingController::State::Idle) {
        m_closeAfterStop = true;
        setStatus(QStringLiteral("Finalizando la grabación antes de cerrar…"));
        if (m_recorder->state() == RecordingController::State::Recording)
            m_recorder->stop();
        event->ignore();
        return;
    }

    m_closing = true;
    m_liveTimer->stop();
    m_settings.windowGeometry = QString::fromLatin1(saveGeometry().toHex());
    AppSettings::save(m_settings);
    m_hotkeys->unregisterAll();

    // Si la inicializacion sigue en curso, esperamos a que termine antes de cerrar.
    if (m_initThread) {
        m_status->setText(QStringLiteral("Esperando a que termine la inicialización…"));
        m_initThread->wait();
        m_initThread->deleteLater();
        m_initThread = nullptr;
    }

    m_overlay->hide();
    m_camPreview->destroyDisplay();
    m_preview->destroyDisplay();
    m_recorder->releaseSources();
    m_engine.shutdown();
    delete m_overlay;
    m_overlay = nullptr;
    event->accept();
}

void MainWindow::applyTheme()
{
    Theme::ensureSkinsFolder();
    const ThemeInfo theme = Theme::find(m_settings.theme);
    qApp->setStyleSheet(Theme::styleSheet(theme.colors));
    const QColor track(theme.colors.track);
    m_emblem = theme.emblem;
    m_logo->setText(QStringLiteral("Re<span style='color:%1'>Kapture</span>%2")
                        .arg(theme.colors.accent, m_emblem.isEmpty() ? QString() : QStringLiteral(" ") + m_emblem));
    m_desktopMeter->setTrackColor(track);
    m_micMeter->setTrackColor(track);
    Logger::info(QStringLiteral("Tema aplicado: %1").arg(theme.name));
}

void MainWindow::updateOverlay()
{
    if (!m_overlay)
        return;
    const bool recording = m_recorder->state() == RecordingController::State::Recording;
    m_overlay->setCorner(m_settings.overlayCorner);
    m_overlay->setOverlayEnabled(m_settings.overlayEnabled);
    m_overlay->setState(recording, recording && m_recordingMic, m_recorder->replayActive(), m_settings.clipSeconds,
                        recording && m_recordingCam);
}

void MainWindow::refreshForeground()
{
    const GameNaming::WindowInfo w = GameNaming::foreground();
    if (!w.isEmpty())
        m_lastForeground = w;
}

QString MainWindow::currentGameName() const
{
    GameNaming::WindowInfo w;
    const int mode = m_settings.captureMode;
    if (mode == 2 && m_recorder->gameHooked()) {
        w.title = m_recorder->hookedTitle();
        w.exeName = m_recorder->hookedExe();
    } else if (mode >= 1 && !m_settings.windowId.isEmpty()) {
        w = GameNaming::fromObsWindowId(m_settings.windowId);
    }
    if (w.isEmpty())
        w = m_lastForeground;
    return GameNaming::displayName(w);
}

void MainWindow::saveClip()
{
    if (!m_engineReady)
        return;
    refreshForeground();
    QString error;
    if (!m_recorder->saveClip(buildOptions(), &error)) {
        setStatus(error, true);
        if (m_settings.soundCues)
            SoundCues::play(SoundCues::Cue::Error, false);
        return;
    }
    setStatus(QStringLiteral("Guardando clip de los últimos %1 s…").arg(m_settings.clipSeconds));
}

void MainWindow::toggleReplay()
{
    if (!m_engineReady || m_recorder->state() != RecordingController::State::Idle) {
        setStatus(QStringLiteral("La repetición instantánea solo se puede alternar cuando no se está grabando."), true);
        return;
    }
    m_replayToggle->setChecked(!m_settings.instantReplay); // dispara onReplayToggled
}

void MainWindow::onReplayToggled(bool on)
{
    m_settings.instantReplay = on;
    AppSettings::save(m_settings);
    refreshIdleState();
    updateOverlay();
    setStatus(on ? QStringLiteral("Repetición instantánea activada: se guardan los últimos %1 s.").arg(m_settings.clipSeconds)
                 : QStringLiteral("Repetición instantánea desactivada."));
    if (m_settings.soundCues)
        SoundCues::play(on ? SoundCues::Cue::Start : SoundCues::Cue::Stop, false);
}

void MainWindow::onClipSaved(const QString &path)
{
    if (m_settings.soundCues)
        SoundCues::play(SoundCues::Cue::Clip, false);
    if (m_overlay)
        m_overlay->flash(QStringLiteral("CLIP GUARDADO"));
    m_library->markDirty();
    const QString text = path.isEmpty() ? QStringLiteral("Clip guardado.")
                                        : QStringLiteral("Clip guardado:\n%1").arg(QDir::toNativeSeparators(path));
    if (m_recorder->state() == RecordingController::State::Recording)
        setStatus(text + QStringLiteral("\n\n") + QStringLiteral("Grabando en:\n") + m_filesText);
    else
        setStatus(text);
}

void MainWindow::onReplayStateChanged(bool)
{
    updateOverlay();
}

void MainWindow::onPerformanceWarning(const QString &text)
{
    Logger::warn(QStringLiteral("Aviso de rendimiento: %1").arg(text));
    if (!m_settings.autoDisableReplay || !m_settings.instantReplay)
        return;

    m_settings.instantReplay = false;
    {
        QSignalBlocker blocker(m_replayToggle);
        m_replayToggle->setChecked(false);
    }
    AppSettings::save(m_settings);
    refreshIdleState();
    updateOverlay();
    setStatus(QStringLiteral("Repetición instantánea desactivada automáticamente para no afectar al juego.\n%1").arg(text), true);
    if (m_settings.soundCues)
        SoundCues::play(SoundCues::Cue::Error, false);
}

void MainWindow::onNavClicked(int page)
{
    m_pages->setCurrentIndex(page);
    updatePreviewActivity(); // fuera de la pestana Grabar no se dibujan las vistas previas (ahorra GPU)
}

void MainWindow::populateWebcams()
{
    QSignalBlocker blocker(m_webcamCombo);
    m_webcamCombo->clear();
    for (const DeviceInfo &d : m_recorder->listWebcams())
        m_webcamCombo->addItem(d.name, d.id);
    if (m_webcamCombo->count() == 0)
        m_webcamCombo->addItem(QStringLiteral("(No se detectó ninguna cámara)"), QString());
    selectById(m_webcamCombo, m_settings.webcamDeviceId);
    if (!m_webcamCombo->currentData().toString().isEmpty())
        m_settings.webcamDeviceId = m_webcamCombo->currentData().toString();
    updateControls();
}

void MainWindow::onWebcamToggled(bool on)
{
    m_settings.webcamEnabled = on;
    if (on)
        m_settings.webcamDeviceId = m_webcamCombo->currentData().toString();
    AppSettings::save(m_settings);
    updateSummary();
    refreshIdleState();
    if (on && m_settings.webcamDeviceId.isEmpty())
        setStatus(QStringLiteral("No se detectó ninguna cámara: conéctala y pulsa ↻."), true);
}

void MainWindow::onWebcamChanged()
{
    if (!m_engineReady)
        return;
    m_settings.webcamDeviceId = m_webcamCombo->currentData().toString();
    AppSettings::save(m_settings);
    refreshIdleState();
}

void MainWindow::onRefreshWebcams()
{
    populateWebcams();
    refreshIdleState();
}

void MainWindow::saveEffects(int slot)
{
    (slot == 0 ? m_settings.desktopEffects : m_settings.micEffects) = m_recorder->effectsJson(slot);
    AppSettings::save(m_settings);
}

void MainWindow::openEffects(int slot)
{
    if (!m_recorder->audioSourceExists(slot)) {
        setStatus(QStringLiteral("Elige primero un dispositivo de audio para poder añadirle efectos."), true);
        return;
    }
    EffectsDialog dialog(m_recorder, slot, this);
    connect(&dialog, &EffectsDialog::changed, this, [this, slot] { saveEffects(slot); });
    dialog.exec();
    saveEffects(slot);
}

int MainWindow::videoBitrateKbps() const
{
    return m_settings.bitrateCustom
               ? m_settings.bitrateKbps
               : EncoderCatalog::bitrateKbps(m_settings.quality, m_settings.width, m_settings.height, m_settings.fps);
}

void MainWindow::syncCameraPreview()
{
    if (!m_camPreview)
        return;
    m_camPreview->setSource(m_recorder->webcamSource());
}

// Decide que vistas previas se dibujan. Se apagan fuera de la pestana Grabar y al minimizar (ahorra
// GPU), y la principal se pausa mientras se graba un monitor con la ventana de ReKapture visible
// (si no, la imagen se reflejaria a si misma en un efecto espejo infinito).
void MainWindow::updatePreviewActivity()
{
    if (!m_pages || !m_preview || !m_camPreview)
        return;

    const bool onRecordPage = m_pages->currentIndex() == 0 && !isMinimized();
    const bool busy = m_recorder->state() != RecordingController::State::Idle;
    const bool mirror = busy && m_settings.captureMode == 0 && !m_settings.hideAppFromCapture;

    m_preview->setPreviewEnabled(m_settings.previewEnabled && onRecordPage && !mirror);
    if (!m_settings.previewEnabled) {
        m_previewPlaceholder->setText(QStringLiteral("Previsualización desactivada\nahorra recursos del equipo"));
        m_previewStack->setCurrentIndex(1);
    } else if (mirror) {
        m_previewPlaceholder->setText(QStringLiteral("Previsualización en pausa mientras grabas este monitor\n"
                                                     "(evita el efecto espejo con la ventana de ReKapture)"));
        m_previewStack->setCurrentIndex(1);
    } else {
        m_previewStack->setCurrentIndex(0);
    }

    m_camPreview->setPreviewEnabled(m_settings.webcamPreview && onRecordPage);
    m_camStack->setCurrentIndex(m_settings.webcamPreview ? 0 : 1);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        updatePreviewActivity();
}

void MainWindow::onWebcamPreviewToggled(bool on)
{
    m_settings.webcamPreview = on;
    if (on && m_settings.webcamDeviceId.isEmpty())
        m_settings.webcamDeviceId = m_webcamCombo->currentData().toString();
    AppSettings::save(m_settings);
    updatePreviewActivity();
    refreshIdleState(); // abre o cierra la camara
    if (on && m_settings.webcamDeviceId.isEmpty())
        setStatus(QStringLiteral("No se detectó ninguna cámara: conéctala y pulsa ↻."), true);
}
