#include "MainWindow.h"

#ifdef Q_OS_WIN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <shellapi.h>
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <memory>
#include <optional>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryFile>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QTimer>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

#include "DiagnosticText.h"
#include "HullEditorPanel.h"
#include "ShipListModel.h"
#include "SteamLocator.h"
#include "StylePanel.h"
#include "nlhc/hull/HullAssembler.h"
#include "nlhc/hull/Validate.h"
#include "nlhc/Version.h"
#include "view/HullViewport.h"

#include <utility>

namespace nlhc {
namespace {

// Qt's own file dialog rather than the Windows one: the native dialog hosts an
// Explorer view inside this process, and an exception thrown by any shell
// extension in its paste path terminates the whole application (0xc000041d).
const QFileDialog::Options kFileDialogOptions = QFileDialog::DontUseNativeDialog;

QString assetProbePath()
{
    return QStringLiteral("catalog.json");
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(1440, 900);
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("files/recent")).toStringList();

    m_packageError.clear();
    const QStringList appArgs = QCoreApplication::arguments();
    const int assetsIndex = appArgs.indexOf(QStringLiteral("--assets"));
    const QString root = assetsIndex >= 0 && assetsIndex + 1 < appArgs.size()
        ? appArgs.at(assetsIndex + 1)
        : locateAssetsRoot();
    QString error;
    if (root.isEmpty() || !m_package.load(root, &error)) {
        m_packageError = root.isEmpty()
            ? tr("Asset package not found. Reinstall the application or restore assets/hulls.")
            : error;
    }

    m_document.setPackage(&m_package);

    i18n::apply(qApp, i18n::storedLanguage());
    buildUi();
    buildMenus();
    retranslateMenus();
    updateRecentFiles();
    updateWindowTitle();
    updateActionsEnabled();
}

MainWindow::~MainWindow() = default;

QString MainWindow::locateAssetsRoot() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/assets/hulls"),
        appDir + QStringLiteral("/../assets/hulls"),
        QStringLiteral(NLHC_SOURCE_DIR "/assets/hulls"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate + QLatin1Char('/') + assetProbePath()))
            return candidate;
    }
    return {};
}

void MainWindow::buildUi()
{
    m_shipModel = new ShipListModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_shipModel);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1);

    auto *leftPanel = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    m_filter = new QLineEdit(leftPanel);
    m_filter->setPlaceholderText(tr("Filter ships"));
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxy->setFilterFixedString(text);
        if (m_proxy->rowCount() > 0 && !m_proxy->mapFromSource(m_shipModel->index(currentShipRow(), 0)).isValid())
            selectShip(0);
    });
    auto *table = new QTableView(leftPanel);
    table->setModel(m_proxy);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    table->setColumnWidth(0, 32);
    table->setColumnWidth(3, 54);
    table->setColumnWidth(4, 54);
    table->setMinimumWidth(240);
    connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                if (current.isValid())
                    selectShip(current.data(int(ShipListModel::Role::ShipIndex)).toInt());
            });
    leftLayout->addWidget(m_filter);
    leftLayout->addWidget(table, 1);

    m_viewport = new HullViewport(this);
    m_viewport->setPackage(m_package.isValid() ? &m_package : nullptr);

    auto *rightScroll = new QScrollArea(this);
    rightScroll->setWidgetResizable(true);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *rightPanel = new QWidget(rightScroll);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    m_editor = new HullEditorPanel(rightPanel);
    m_editor->setPackage(m_package.isValid() ? &m_package : nullptr);
    connect(m_editor, &HullEditorPanel::configChanged, this, &MainWindow::applyConfig);
    m_stylePanel = new StylePanel(rightPanel);
    QSettings settings;
    RenderStyle style;
    style.baseColor = settings.value(QStringLiteral("style/baseColor"), style.baseColor.name()).toString();
    style.trimColor = settings.value(QStringLiteral("style/trimColor"), style.trimColor.name()).toString();
    m_stylePanel->setStyle(style);
    m_viewport->setStyle(style);
    connect(m_stylePanel, &StylePanel::styleChanged, this, [this](const RenderStyle &newStyle) {
        // The panel owns the preview colours only; the paint offset belongs to
        // the selected ship, so re-apply it here - otherwise a slider move sends
        // an offset of zero and the paint pattern jumps on screen.
        const int row = currentShipRow();
        const auto config = row >= 0 ? m_document.hullConfig(row) : std::nullopt;
        RenderStyle style = newStyle;
        if (config)
            style.paintOffset = config->textureVariation;
        m_viewport->setStyle(style);
        QSettings settings;
        settings.setValue(QStringLiteral("style/baseColor"), newStyle.baseColor.name());
        settings.setValue(QStringLiteral("style/trimColor"), newStyle.trimColor.name());
        // Base and trim are preview-only, but the hull tint is a fleet field.
        if (!config || config->hullTint == newStyle.hullTint)
            return;
        RandomHullConfig updated = *config;
        updated.hullTint = newStyle.hullTint;
        if (m_document.setHullConfig(row, updated))
            updateWindowTitle();
    });
    rightLayout->addWidget(m_editor, 1);
    rightLayout->addWidget(m_stylePanel);
    rightScroll->setWidget(rightPanel);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(m_viewport);
    m_splitter->addWidget(rightScroll);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes({300, 760, 360});
    m_splitter->setChildrenCollapsible(false);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] { m_userAdjustedSplitter = true; });
    leftPanel->setMinimumWidth(240);
    rightScroll->setMinimumWidth(320);
    setCentralWidget(m_splitter);

    m_diagnosticList = new QListWidget(this);
    m_diagnosticDock = new QDockWidget(this);
    m_diagnosticDock->setObjectName(QStringLiteral("diagnosticsDock"));
    m_diagnosticDock->setWidget(m_diagnosticList);
    m_diagnosticDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_diagnosticDock);
    // Empty until a hull is inspected, so it starts collapsed instead of taking a
    // quarter of the window on every launch. The View menu brings it back.
    m_diagnosticDock->hide();

    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);
    m_shipModel->setDocument(&m_document);
}

void MainWindow::buildMenus()
{
    // The menu bar is rebuilt on every language switch, so any list of actions collected
    // from the old menus has to be dropped here: the moved-from pointers are dangling and
    // the retranslation would no longer match the rebuilt actions.
    m_languageActions.clear();
    auto *toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));

    m_fileMenu = menuBar()->addMenu(QString());
    m_openAction = m_fileMenu->addAction(QString());
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFileDialog);
    m_saveAction = m_fileMenu->addAction(QString());
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveFile);
    m_saveAsAction = m_fileMenu->addAction(QString());
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveFileAs);
    m_recentMenu = m_fileMenu->addMenu(QString());
    m_fileMenu->addSeparator();
    m_exportAction = m_fileMenu->addAction(QString());
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportImage);
    m_fileMenu->addSeparator();
    m_quitAction = m_fileMenu->addAction(QString());
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_editMenu = menuBar()->addMenu(QString());
    m_randomizeAction = m_editMenu->addAction(QString());
    connect(m_randomizeAction, &QAction::triggered, this, &MainWindow::randomizeCurrent);
    m_randomizeFleetAction = m_editMenu->addAction(QString());
    connect(m_randomizeFleetAction, &QAction::triggered, this, &MainWindow::randomizeFleet);
    m_copyAction = m_editMenu->addAction(QString());
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copyToSameType);
    m_repairAction = m_editMenu->addAction(QString());
    connect(m_repairAction, &QAction::triggered, this, [this] {
        const int row = currentShipRow();
        if (row < 0)
            return;
        const ShipRecord ship = m_document.ship(row);
        auto config = m_document.hullConfig(row);
        if (!config)
            return;
        RandomHullConfig repaired = *config;
        if (repairConfig(m_package, ship.hullType, &repaired)) {
            m_document.setHullConfig(row, repaired);
            refreshEditor();
            refreshAssembly();
            updateWindowTitle();
        }
    });

    // The debug views are how a mismatch is told apart: flat colours name the material that owns
    // a face, and the mask/albedo views show what the shader actually samples there.
    m_viewMenu = menuBar()->addMenu(QString());
    m_debugActions.clear();
    const QList<QPair<QString, QString>> debugEntries = {
        {QStringLiteral("off"), QString()},
        {QStringLiteral("materials"), QString()},
        {QStringLiteral("mask"), QString()},
        {QStringLiteral("albedo"), QString()},
        {QStringLiteral("uv"), QString()},
    };
    for (const auto &entry : debugEntries) {
        QAction *action = m_viewMenu->addAction(QString());
        action->setCheckable(true);
        action->setData(entry.first);
        connect(action, &QAction::triggered, this, [this, action] {
            setDebugViewName(action->data().toString());
        });
        m_debugActions.append(action);
    }
    setDebugViewName(QStringLiteral("off"));

    if (m_diagnosticDock) {
        m_viewMenu->addSeparator();
        QAction *toggle = m_diagnosticDock->toggleViewAction();
        toggle->setObjectName(QStringLiteral("diagnosticsToggleAction"));
        m_viewMenu->addAction(toggle);
    }

    m_languageMenu = menuBar()->addMenu(QString());
    m_helpMenu = menuBar()->addMenu(QString());
    m_aboutAction = m_helpMenu->addAction(QString());
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    const QList<QPair<i18n::Language, QAction *>> languageEntries = {
        {i18n::Language::System, m_languageMenu->addAction(QString())},
        {i18n::Language::English, m_languageMenu->addAction(QString())},
        {i18n::Language::Chinese, m_languageMenu->addAction(QString())},
    };
    for (const auto &entry : languageEntries) {
        QAction *action = entry.second;
        action->setCheckable(true);
        action->setData(int(entry.first));
        connect(action, &QAction::triggered, this, [this, action] {
            applyLanguage(i18n::Language(action->data().toInt()));
        });
        m_languageActions.append(action);
    }

    toolbar->addAction(m_openAction);
    toolbar->addAction(m_saveAction);
    toolbar->addSeparator();
    toolbar->addAction(m_randomizeAction);
}

void MainWindow::retranslateMenus()
{
    m_fileMenu->setTitle(tr("&File"));
    m_openAction->setText(tr("&Open fleet..."));
    m_saveAction->setText(tr("&Save"));
    m_saveAsAction->setText(tr("Save &as..."));
    m_recentMenu->setTitle(tr("Recent"));
    m_exportAction->setText(tr("Export view as PNG..."));
    m_quitAction->setText(tr("E&xit"));
    m_editMenu->setTitle(tr("&Edit"));
    m_randomizeAction->setText(tr("&Randomize hull"));
    m_randomizeFleetAction->setText(tr("Randomize whole fleet"));
    m_copyAction->setText(tr("Copy hull to same ship type"));
    m_repairAction->setText(tr("Repair hull configuration"));
    m_viewMenu->setTitle(tr("&View"));
    if (m_debugActions.size() == 5) {
        m_debugActions.at(0)->setText(tr("&Off"));
        m_debugActions.at(1)->setText(tr("&Materials"));
        m_debugActions.at(2)->setText(tr("&Mask"));
        m_debugActions.at(3)->setText(tr("&Albedo"));
        m_debugActions.at(4)->setText(tr("&UV"));
    }
    m_languageMenu->setTitle(tr("&Language"));
    m_helpMenu->setTitle(tr("&Help"));
    m_aboutAction->setText(tr("&About Liner Hull Customizer"));
    // Driven by the language each action carries, so the order and count cannot drift out of
    // sync with what buildMenus() created.
    for (QAction *action : m_languageActions) {
        switch (i18n::Language(action->data().toInt())) {
        case i18n::Language::English:
            action->setText(QStringLiteral("English"));
            break;
        case i18n::Language::Chinese:
            action->setText(QStringLiteral("中文"));
            break;
        default:
            action->setText(tr("System default"));
            break;
        }
    }
    if (m_diagnosticDock) {
        m_diagnosticDock->setWindowTitle(tr("Diagnostics"));
        if (auto *toggle = m_diagnosticDock->findChild<QAction *>(QStringLiteral("diagnosticsToggleAction")))
            toggle->setText(tr("&Diagnostics"));
    }
    if (m_filter)
        m_filter->setPlaceholderText(tr("Filter ships"));
    updateWindowTitle();
}

void MainWindow::applyLanguage(i18n::Language language)
{
    i18n::storeLanguage(language);
    i18n::apply(qApp, language);

    const int row = currentShipRow();
    const bool diagnosticsVisible = m_diagnosticDock && !m_diagnosticDock->isHidden();
    // The rebuild below replaces the splitter and the dock, and the defaults
    // buildUi() installs are not the proportions this window is using, so carry
    // the current geometry across or the layout jumps on every language change.
    const QList<int> splitterSizes = m_splitter ? m_splitter->sizes() : QList<int>();
    const int dockHeight = diagnosticsVisible ? m_diagnosticDock->height() : 0;
    m_handlingLanguageChange = true;
    delete takeCentralWidget();
    delete m_diagnosticDock;
    m_diagnosticDock = nullptr;
    m_diagnosticList = nullptr;
    m_statusLabel = nullptr;
    menuBar()->clear();
    const QList<QToolBar *> toolbars = findChildren<QToolBar *>();
    for (QToolBar *toolbar : toolbars)
        delete toolbar;
    buildUi();
    buildMenus();
    if (diagnosticsVisible && m_diagnosticDock)
        m_diagnosticDock->show();
    QTimer::singleShot(0, this, [this, splitterSizes, dockHeight] {
        if (m_splitter && splitterSizes.size() == 3 && splitterSizes.at(1) > 0)
            m_splitter->setSizes(splitterSizes);
        else
            applySplitterRatio();
        if (dockHeight > 0 && m_diagnosticDock && !m_diagnosticDock->isHidden())
            resizeDocks({m_diagnosticDock}, {dockHeight}, Qt::Vertical);
    });
    retranslateMenus();
    updateRecentFiles();
    updateActionsEnabled();
    m_handlingLanguageChange = false;
    if (row >= 0)
        selectShip(row);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_splitter && !m_splitterSized) {
        m_splitterSized = true;
        QTimer::singleShot(0, this, [this] {
            applySplitterRatio();
            if (m_diagnosticDock)
                resizeDocks({m_diagnosticDock}, {160}, Qt::Vertical);
        });
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange && !m_handlingLanguageChange)
        retranslateMenus();
    QMainWindow::changeEvent(event);
}

void MainWindow::updateWindowTitle()
{
    const QString file = m_document.filePath().isEmpty() ? tr("No fleet") : QFileInfo(m_document.filePath()).fileName();
    const QString dirty = m_document.isDirty() ? QStringLiteral("*") : QString();
    setWindowTitle(QStringLiteral("Liner Hull Customizer — %1%2").arg(file, dirty));
    if (m_saveAction)
        m_saveAction->setEnabled(!m_document.filePath().isEmpty());
}

void MainWindow::updateActionsEnabled()
{
    const int row = currentShipRow();
    const bool hasShip = row >= 0;
    const bool editable = hasShip && m_document.ship(row).editable;
    if (m_randomizeAction)
        m_randomizeAction->setEnabled(editable);
    if (m_copyAction)
        m_copyAction->setEnabled(editable);
    if (m_repairAction)
        m_repairAction->setEnabled(editable);
    if (m_randomizeFleetAction)
        m_randomizeFleetAction->setEnabled(m_package.isValid() && m_document.shipCount() > 0);
    if (m_exportAction)
        m_exportAction->setEnabled(hasShip);
    updateWindowTitle();
}

int MainWindow::currentShipRow() const
{
    if (!m_shipModel)
        return -1;
    return m_currentRow;
}

void MainWindow::openFileDialog()
{
    if (!confirmDiscardChanges())
        return;
    // Start where the game keeps its fleets; fall back to the folder of the file
    // already open when Steam is not installed here.
    QString start = locateFleetFolder();
    if (start.isEmpty() && !m_document.filePath().isEmpty())
        start = QFileInfo(m_document.filePath()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open fleet"), start,
                                                      tr("Fleet files (*.fleet);;All files (*)"), nullptr,
                                                      kFileDialogOptions);
    if (path.isEmpty())
        return;
    openFile(path);
}

bool MainWindow::openFile(const QString &path)
{
    QVector<Diagnostic> diagnostics;
    QString error;
    if (!m_document.load(path, &diagnostics, &error)) {
        QMessageBox::critical(this, tr("Open failed"), error);
        return false;
    }
    m_document.setPackage(&m_package);
    m_shipModel->setDocument(&m_document);
    m_shipModel->refresh();
    updateRecentFiles(path);
    updateActionsEnabled();
    if (m_shipModel->rowCount() > 0)
        selectShip(0);
    else
        refreshAssembly();
    return true;
}

bool MainWindow::saveFile()
{
    if (m_document.filePath().isEmpty())
        return saveFileAs();

    switch (trySave(m_document.filePath())) {
    case SaveOutcome::Saved:
        updateWindowTitle();
        return true;
    case SaveOutcome::SaveAs:
        return saveFileAs();
    case SaveOutcome::Aborted:
        break;
    }
    return false;
}

bool MainWindow::saveFileAs()
{
    // Same preference as Open: the game's own folder when Steam has it, else the
    // path of the file being edited.
    const QString fileName = m_document.filePath().isEmpty()
        ? QStringLiteral("fleet.fleet")
        : QFileInfo(m_document.filePath()).fileName();
    QString start = locateFleetFolder();
    if (!start.isEmpty()) {
        start = QDir(start).filePath(fileName);
    } else if (!m_document.filePath().isEmpty()) {
        start = m_document.filePath();
    }

    for (;;) {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save fleet as"), start,
                                                          tr("Fleet files (*.fleet)"), nullptr,
                                                          kFileDialogOptions);
        if (path.isEmpty())
            return false;
        const SaveOutcome outcome = trySave(path);
        if (outcome == SaveOutcome::SaveAs) {
            start = path;
            continue;
        }
        if (outcome != SaveOutcome::Saved)
            return false;
        updateRecentFiles(path);
        updateWindowTitle();
        return true;
    }
}

MainWindow::SaveOutcome MainWindow::trySave(const QString &path)
{
    QString error;
    QString notice;
    if (m_document.save(path, &error, &notice)) {
        reportSaved(path, notice);
        return SaveOutcome::Saved;
    }

    // A refused write is nearly always a rights problem, so the automatic retry is
    // the elevated one; only when that fails too does the user have to decide.
    QString elevatedError;
    QString elevatedNotice;
    if (saveElevated(path, &elevatedError, &elevatedNotice)) {
        reportSaved(path, elevatedNotice);
        return SaveOutcome::Saved;
    }

    return askSaveElsewhere(error, elevatedError) ? SaveOutcome::SaveAs : SaveOutcome::Aborted;
}

bool MainWindow::saveElevated(const QString &path, QString *error, QString *notice)
{
#ifdef Q_OS_WIN
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    // The parent was refused the write, so the finished bytes go to a staged file the
    // elevated child can read; the child reports back through a result file next to it.
    // Security software that guards the save folder often blocks %TEMP% too, so every
    // plausible staging folder gets a turn before the retry is abandoned.
    QStringList stagingDirs{QDir::tempPath(),
                            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
                            QCoreApplication::applicationDirPath(),
                            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)};
    stagingDirs.removeAll(QString());
    stagingDirs.removeDuplicates();

    const QByteArray bytes = m_document.serialized();
    std::unique_ptr<QTemporaryFile> payload;
    for (const QString &dir : stagingDirs) {
        QDir().mkpath(dir);
        auto candidate = std::make_unique<QTemporaryFile>(
            QDir(dir).filePath(QStringLiteral("nlhc-elevated-XXXXXX.fleet")));
        if (candidate->open() && candidate->write(bytes) == bytes.size() && candidate->flush()) {
            candidate->close();
            payload = std::move(candidate);
            break;
        }
    }
    if (!payload)
        return fail(tr("Cannot stage the file for an elevated save: %1").arg(bytes.isEmpty()
                                                                                 ? tr("the fleet is empty")
                                                                                 : tr("every candidate folder refused it")));
    const QString payloadPath = payload->fileName();
    const QString resultPath = payloadPath + QStringLiteral(".result");

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.nShow = SW_HIDE;
    info.lpVerb = L"runas";
    const std::wstring executable =
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const std::wstring parameters =
        QStringLiteral("--elevated-save \"%1\" \"%2\"").arg(payloadPath, path).toStdWString();
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.c_str();

    // The consent prompt is modal on the secure desktop, so the cursor is set before
    // the call: the window behind it should already look busy when focus returns.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!ShellExecuteExW(&info)) {
        QApplication::restoreOverrideCursor();
        const DWORD failure = GetLastError();
        if (failure == ERROR_CANCELLED)
            return fail(tr("Administrator rights were declined, so nothing was written."));
        return fail(tr("Cannot start the elevated save (error %1).").arg(int(failure)));
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    QApplication::restoreOverrideCursor();
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);

    QFile result(resultPath);
    QString message;
    if (result.open(QIODevice::ReadOnly))
        message = QString::fromUtf8(result.readAll()).trimmed();
    result.close();
    QFile::remove(resultPath);

    if (code != 0)
        return fail(message.isEmpty() ? tr("The elevated save did not complete.") : message);

    if (notice)
        *notice = message;
    m_document.markSaved(path);
    return true;
#else
    Q_UNUSED(path)
    Q_UNUSED(notice)
    if (error)
        *error = tr("Saving as administrator is only available on Windows.");
    return false;
#endif
}

void MainWindow::reportSaved(const QString &path, const QString &backupProblem)
{
    if (backupProblem.isEmpty()) {
        statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
        return;
    }
    statusBar()->showMessage(tr("Saved %1, but no backup was kept: %2").arg(path, backupProblem), 8000);
}

bool MainWindow::askSaveElsewhere(const QString &error, const QString &elevatedError)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(tr("Save failed"));
    box.setText(tr("The fleet could not be written."));
    box.setInformativeText(error + QStringLiteral("\n\n")
                           + tr("The elevated retry failed too: %1").arg(elevatedError)
                           + QStringLiteral("\n\n")
                           + tr("The folder may be read-only, or security software may be blocking "
                                "this program from writing to it. Save the fleet to another folder "
                                "instead."));
    QPushButton *saveAsButton = box.addButton(tr("Save as…"), QMessageBox::AcceptRole);
    QPushButton *cancelButton = box.addButton(QMessageBox::Cancel);
    box.setEscapeButton(cancelButton);
    box.setDefaultButton(saveAsButton);
    box.exec();
    return box.clickedButton() == saveAsButton;
}

void MainWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export view"), QStringLiteral("hull.png"),
                                                      tr("PNG image (*.png)"), nullptr, kFileDialogOptions);
    if (path.isEmpty())
        return;
    const QImage image = m_viewport->grabImage();
    if (image.isNull() || !image.save(path))
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(path));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Liner Hull Customizer"),
                       tr("<b>Nebulous Liner Hull Customizer</b><br/>Version %1<br/><br/>"
                          "Customizes the modular hull of the Protectorate Bulk Hauler and "
                          "Container Hauler liners in .fleet files.").arg(nlhc::versionString()));
}

void MainWindow::updateRecentFiles(const QString &path)
{
    if (path.isEmpty()) {
        QSettings settings;
        m_recentFiles = settings.value(QStringLiteral("files/recent")).toStringList();
    } else {
        m_recentFiles.removeAll(path);
        m_recentFiles.prepend(path);
        while (m_recentFiles.size() > 8)
            m_recentFiles.removeLast();
        QSettings settings;
        settings.setValue(QStringLiteral("files/recent"), m_recentFiles);
    }

    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    for (const QString &recent : std::as_const(m_recentFiles)) {
        QAction *action = m_recentMenu->addAction(QFileInfo(recent).fileName());
        action->setToolTip(recent);
        connect(action, &QAction::triggered, this, [this, recent] {
            if (confirmDiscardChanges())
                openFile(recent);
        });
    }
    m_recentMenu->setEnabled(!m_recentFiles.isEmpty());
}

bool MainWindow::confirmDiscardChanges()
{
    if (!m_document.isDirty())
        return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"),
                                              tr("Save changes to %1 before continuing?").arg(
                                                  QFileInfo(m_document.filePath()).fileName()),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveFile();
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (confirmDiscardChanges())
        event->accept();
    else
        event->ignore();
}

bool MainWindow::openFleet(const QString &path)
{
    return openFile(path);
}

void MainWindow::applySplitterRatio()
{
    if (!m_splitter || m_userAdjustedSplitter)
        return;
    const int width = qMax(m_splitter->width(), 600);
    const int left = int(width * 0.22);
    const int right = int(width * 0.27);
    m_splitter->setSizes({left, width - left - right, right});
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    applySplitterRatio();
}

bool MainWindow::applyLanguageByName(const QString &name)
{
    i18n::Language language = i18n::Language::System;
    if (name == QLatin1String("en"))
        language = i18n::Language::English;
    else if (name == QLatin1String("zh"))
        language = i18n::Language::Chinese;
    else if (name != QLatin1String("system"))
        return false;
    applyLanguage(language);
    return true;
}

void MainWindow::setDebugViewName(const QString &name)
{
    HullRenderer::DebugView view = HullRenderer::DebugView::Off;
    if (name == QLatin1String("materials"))
        view = HullRenderer::DebugView::Materials;
    else if (name == QLatin1String("mask"))
        view = HullRenderer::DebugView::Mask;
    else if (name == QLatin1String("albedo"))
        view = HullRenderer::DebugView::Albedo;
    else if (name == QLatin1String("uv"))
        view = HullRenderer::DebugView::Uv;
    if (m_viewport)
        m_viewport->setDebugView(view);
    const QString current = view == HullRenderer::DebugView::Off ? QStringLiteral("off") : name;
    for (QAction *action : m_debugActions)
        action->setChecked(action->data().toString() == current);
}

void MainWindow::setOrbitAngles(float yawDegrees, float pitchDegrees)
{
    if (m_viewport)
        m_viewport->setOrbitAngles(yawDegrees, pitchDegrees);
}

QString MainWindow::debugRenderLegend() const
{
    return m_viewport ? m_viewport->debugLegend() : QString();
}

QString MainWindow::debugAssemblySummary() const
{
    return m_lastAssemblySummary;
}

QString MainWindow::summarizeAssembly(const AssemblyResult &assembly) const
{
    int painted = 0;
    int unpainted = 0;
    int withAlbedo = 0;
    int withMask = 0;
    int nullMaterial = 0;
    for (const DrawItem &item : assembly.items) {
        if (!item.material) {
            ++nullMaterial;
            continue;
        }
        item.material->paintable ? ++painted : ++unpainted;
        if (!item.material->albedoPath.isEmpty())
            ++withAlbedo;
        if (!item.material->maskPath.isEmpty())
            ++withMask;
    }
    const QVector3D size = assembly.bounds.size();
    return QStringLiteral("items=%1 painted=%2 unpainted=%3 albedo=%4 mask=%5 nullMat=%6 size=%7x%8x%9")
        .arg(assembly.items.size())
        .arg(painted)
        .arg(unpainted)
        .arg(withAlbedo)
        .arg(withMask)
        .arg(nullMaterial)
        .arg(size.z(), 0, 'f', 1)
        .arg(size.y(), 0, 'f', 1)
        .arg(size.x(), 0, 'f', 1);
}

QString MainWindow::debugLayoutSummary() const
{
    QString summary = QStringLiteral("window=%1x%2").arg(width()).arg(height());
    if (m_splitter)
        summary += QStringLiteral(" splitter=%1").arg(QString::number(m_splitter->width()));
    if (m_viewport)
        summary += QStringLiteral(" viewport=%1x%2").arg(m_viewport->width()).arg(m_viewport->height());
    return summary;
}

void MainWindow::selectShipByIndex(int row)
{
    selectShip(row);
}

void MainWindow::selectShip(int row)
{
    m_currentRow = row;
    refreshEditor();
    refreshAssembly();
    refreshDiagnostics();
    updateActionsEnabled();
}

void MainWindow::refreshEditor()
{
    const int row = currentShipRow();
    if (row < 0) {
        m_editor->setHullType(nullptr);
        m_editor->setEditable(false);
        return;
    }
    const ShipRecord ship = m_document.ship(row);
    const HullTypeDef *hullType = m_package.isValid() ? m_package.hullType(ship.hullType) : nullptr;
    m_editor->setHullType(hullType);
    auto config = m_document.hullConfig(row);
    if (config)
        m_editor->setConfig(*config);
    m_editor->setEditable(ship.editable);
    if (config) {
        RenderStyle style = m_viewport->style();
        style.paintOffset = config->textureVariation;
        style.hullTint = config->hullTint;
        m_viewport->setStyle(style);
        // The panel emits a complete RenderStyle on every edit, so it has to
        // carry the per-ship paint offset as well: without this, moving the tint
        // slider sent an offset of zero and the paint pattern jumped.
        m_stylePanel->setStyle(style);
    }
    if (!ship.editable)
        statusBar()->showMessage(readOnlyReasonText(ship, m_package), 8000);
    else if (!m_packageError.isEmpty())
        statusBar()->showMessage(m_packageError, 8000);
}

void MainWindow::refreshAssembly()
{
    const int row = currentShipRow();
    if (row < 0 || !m_package.isValid()) {
        m_viewport->setAssembly(AssemblyResult{});
        return;
    }
    const ShipRecord ship = m_document.ship(row);
    auto config = m_document.hullConfig(row);
    if (!config) {
        m_viewport->setAssembly(AssemblyResult{});
        return;
    }
    HullAssembler assembler;
    m_viewport->setPackage(&m_package);
    m_lastAssembly = assembler.assemble(m_package, ship.hullType, *config);
    m_lastAssemblySummary = summarizeAssembly(m_lastAssembly);
    m_viewport->setAssembly(m_lastAssembly);
}

void MainWindow::refreshDiagnostics()
{
    if (!m_diagnosticList)
        return;
    m_diagnosticList->clear();
    const int row = currentShipRow();
    if (row < 0)
        return;
    const ShipRecord ship = m_document.ship(row);
    QVector<Diagnostic> diagnostics;
    auto config = m_document.hullConfig(row);
    if (config && m_package.isValid())
        diagnostics = checkConfig(m_package, ship.hullType, *config);
    for (const Diagnostic &diagnostic : std::as_const(diagnostics)) {
        auto *item = new QListWidgetItem(diagnosticText(diagnostic), m_diagnosticList);
        if (diagnostic.severity == Severity::Error)
            item->setForeground(QColor(0xd0, 0x4a, 0x3a));
        else if (diagnostic.severity == Severity::Warning)
            item->setForeground(QColor(0xc0, 0x8a, 0x20));
    }
    if (diagnostics.isEmpty())
        m_diagnosticList->addItem(tr("No problems detected."));
    // A fleet can arrive in this state, and then nothing was auto-corrected on
    // the user's behalf - say so by opening the dock. Edits that produce one of
    // these are repaired before they reach here, so they never open it.
    for (const Diagnostic &diagnostic : std::as_const(diagnostics)) {
        if (diagnostic.code == QLatin1String(diag::SecondarySnapOutOfRange)
            || diagnostic.code == QLatin1String(diag::SecondarySegmentOutOfRange)) {
            if (m_diagnosticDock && !m_diagnosticDock->isVisible())
                m_diagnosticDock->show();
            break;
        }
    }
    if (m_repairAction)
        m_repairAction->setEnabled(ship.editable && !diagnostics.isEmpty());
}

void MainWindow::applyConfig(const RandomHullConfig &config)
{
    const int row = currentShipRow();
    if (row < 0)
        return;
    if (m_document.setHullConfig(row, config)) {
        refreshAssembly();
        // Repair before the diagnostics view is rebuilt, so the leftovers it
        // reports are the ones worth opening the dock for.
        remediateSuperstructures(row);
        refreshDiagnostics();
        updateWindowTitle();
    }
}

bool MainWindow::remediateSuperstructures(int row)
{
    if (m_remediating || !m_package.isValid() || row < 0)
        return false;
    const ShipRecord ship = m_document.ship(row);
    const auto config = m_document.hullConfig(row);
    if (!config)
        return false;

    QVector<int> segments;
    for (const Diagnostic &diagnostic : checkConfig(m_package, ship.hullType, *config)) {
        if (diagnostic.code != QLatin1String(diag::SecondarySnapOutOfRange))
            continue;
        if (diagnostic.segment < 0 || diagnostic.segment >= RandomHullConfig::SegmentCount)
            continue;
        if (!segments.contains(diagnostic.segment))
            segments.append(diagnostic.segment);
    }
    if (segments.isEmpty())
        return false;

    // A superstructure aimed at a mount point the segment does not have would
    // crash the game, so drop it here rather than let it reach a save. The status
    // bar says what happened; the dock stays closed unless the user opens it.
    m_remediating = true;
    for (const int segment : std::as_const(segments)) {
        m_editor->disableSecondary(segment);
        statusBar()->showMessage(tr("Removed the %1 superstructure: the segment has no such mount point.")
                                     .arg(segmentName(segment)),
                                 10000);
    }
    m_remediating = false;
    return true;
}

void MainWindow::randomizeCurrent()
{
    const int row = currentShipRow();
    if (row < 0)
        return;
    const ShipRecord ship = m_document.ship(row);
    if (!ship.editable)
        return;
    QRandomGenerator generator = QRandomGenerator::securelySeeded();
    RandomHullConfig config = randomizeConfig(m_package, ship.hullType, generator);
    // The tint is a paint choice the slider owns; re-rolling the hull must not
    // recolour the ship as a side effect.
    if (const auto previous = m_document.hullConfig(row))
        config.hullTint = previous->hullTint;
    m_document.setHullConfig(row, config);
    refreshEditor();
    refreshAssembly();
    refreshDiagnostics();
    updateWindowTitle();
}

void MainWindow::randomizeFleet()
{
    QRandomGenerator generator = QRandomGenerator::securelySeeded();
    for (int row = 0; row < m_document.shipCount(); ++row) {
        const ShipRecord ship = m_document.ship(row);
        if (!ship.editable)
            continue;
        RandomHullConfig config = randomizeConfig(m_package, ship.hullType, generator);
        if (const auto previous = m_document.hullConfig(row))
            config.hullTint = previous->hullTint;
        m_document.setHullConfig(row, config);
    }
    refreshEditor();
    refreshAssembly();
    refreshDiagnostics();
    updateWindowTitle();
}

void MainWindow::copyToSameType()
{
    const int row = currentShipRow();
    if (row < 0)
        return;
    const ShipRecord source = m_document.ship(row);
    auto config = m_document.hullConfig(row);
    if (!config)
        return;
    int applied = 0;
    for (int other = 0; other < m_document.shipCount(); ++other) {
        if (other == row)
            continue;
        const ShipRecord candidate = m_document.ship(other);
        if (candidate.hullType != source.hullType || !candidate.editable)
            continue;
        if (m_document.setHullConfig(other, *config))
            ++applied;
    }
    statusBar()->showMessage(tr("Applied hull to %1 ship(s).").arg(applied), 4000);
    refreshEditor();
    refreshAssembly();
    updateWindowTitle();
}

} // namespace nlhc
