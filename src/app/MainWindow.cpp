#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QComboBox>
#include <QDockWidget>
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
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

#include "HullEditorPanel.h"
#include "ShipListModel.h"
#include "StylePanel.h"
#include "nlhc/hull/HullAssembler.h"
#include "nlhc/hull/Validate.h"
#include "nlhc/Version.h"
#include "view/HullViewport.h"

#include <utility>

namespace nlhc {
namespace {

QString assetProbePath()
{
    return QStringLiteral("catalog.json");
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("files/recent")).toStringList();

    m_packageError.clear();
    const QString root = locateAssetsRoot();
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
    table->setColumnWidth(0, 34);
    table->setColumnWidth(1, 170);
    table->setColumnWidth(2, 140);
    table->setColumnWidth(3, 60);
    table->setColumnWidth(4, 60);
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
        m_viewport->setStyle(newStyle);
        QSettings settings;
        settings.setValue(QStringLiteral("style/baseColor"), newStyle.baseColor.name());
        settings.setValue(QStringLiteral("style/trimColor"), newStyle.trimColor.name());
    });
    connect(m_stylePanel, &StylePanel::textureVariationRerollRequested, this, &MainWindow::rerollTextureVariation);
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
    m_splitter->setSizes({320, 720, 380});
    setCentralWidget(m_splitter);

    m_diagnosticList = new QListWidget(this);
    m_diagnosticDock = new QDockWidget(this);
    m_diagnosticDock->setObjectName(QStringLiteral("diagnosticsDock"));
    m_diagnosticDock->setWidget(m_diagnosticList);
    m_diagnosticDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_diagnosticDock);

    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);
    m_shipModel->setDocument(&m_document);
}

void MainWindow::buildMenus()
{
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
    m_languageMenu->setTitle(tr("&Language"));
    m_helpMenu->setTitle(tr("&Help"));
    m_aboutAction->setText(tr("&About NLHC"));
    if (m_languageActions.size() == 3) {
        m_languageActions[0]->setText(tr("System default"));
        m_languageActions[1]->setText(tr("English"));
        m_languageActions[2]->setText(QStringLiteral("中文"));
    }
    if (m_diagnosticDock)
        m_diagnosticDock->setWindowTitle(tr("Diagnostics"));
    if (m_filter)
        m_filter->setPlaceholderText(tr("Filter ships"));
    updateWindowTitle();
}

void MainWindow::applyLanguage(i18n::Language language)
{
    i18n::storeLanguage(language);
    i18n::apply(qApp, language);

    const int row = currentShipRow();
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
    retranslateMenus();
    updateRecentFiles();
    updateActionsEnabled();
    m_handlingLanguageChange = false;
    if (row >= 0)
        selectShip(row);
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
    setWindowTitle(QStringLiteral("NLHC — %1%2").arg(file, dirty));
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
    const QString path = QFileDialog::getOpenFileName(this, tr("Open fleet"), QString(),
                                                      tr("Fleet files (*.fleet);;All files (*)"));
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
    QString error;
    if (!m_document.save(m_document.filePath(), &error)) {
        QMessageBox::critical(this, tr("Save failed"), error);
        return false;
    }
    updateWindowTitle();
    statusBar()->showMessage(tr("Saved %1").arg(m_document.filePath()), 4000);
    return true;
}

bool MainWindow::saveFileAs()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save fleet as"),
                                                      m_document.filePath().isEmpty()
                                                          ? QStringLiteral("fleet.fleet")
                                                          : m_document.filePath(),
                                                      tr("Fleet files (*.fleet)"));
    if (path.isEmpty())
        return false;
    QString error;
    if (!m_document.save(path, &error)) {
        QMessageBox::critical(this, tr("Save failed"), error);
        return false;
    }
    updateRecentFiles(path);
    updateWindowTitle();
    return true;
}

void MainWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export view"), QStringLiteral("hull.png"),
                                                      tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    const QImage image = m_viewport->grabImage();
    if (image.isNull() || !image.save(path))
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(path));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About NLHC"),
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
    if (config)
        m_stylePanel->setHullTint(config->hullTint);
    if (config)
        m_stylePanel->setTextureVariation(config->textureVariation);
    if (!m_packageError.isEmpty())
        statusBar()->showMessage(m_packageError);
    else if (!ship.editable)
        statusBar()->showMessage(ship.readOnlyReason);
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
    m_viewport->setAssembly(assembler.assemble(m_package, ship.hullType, *config));
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
        auto *item = new QListWidgetItem(describeDiagnostic(diagnostic), m_diagnosticList);
        if (diagnostic.severity == Severity::Error)
            item->setForeground(QColor(0xd0, 0x4a, 0x3a));
        else if (diagnostic.severity == Severity::Warning)
            item->setForeground(QColor(0xc0, 0x8a, 0x20));
    }
    if (diagnostics.isEmpty())
        m_diagnosticList->addItem(tr("No problems detected."));
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
        refreshDiagnostics();
        m_stylePanel->setTextureVariation(config.textureVariation);
        updateWindowTitle();
    }
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
    const RandomHullConfig config = randomizeConfig(m_package, ship.hullType, generator);
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
        m_document.setHullConfig(row, randomizeConfig(m_package, ship.hullType, generator));
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

void MainWindow::rerollTextureVariation()
{
    const int row = currentShipRow();
    if (row < 0)
        return;
    auto config = m_document.hullConfig(row);
    if (!config)
        return;
    QRandomGenerator generator = QRandomGenerator::securelySeeded();
    const auto randomAxis = [&generator] {
        return float(generator.generateDouble() * 2000.0 - 1000.0);
    };
    config->textureVariation = QVector3D(randomAxis(), randomAxis(), randomAxis());
    m_document.setHullConfig(row, *config);
    m_stylePanel->setTextureVariation(config->textureVariation);
    updateWindowTitle();
}

} // namespace nlhc
