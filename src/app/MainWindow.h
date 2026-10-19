#pragma once

#include <QMainWindow>
#include <QStringList>

#include "I18n.h"
#include "nlhc/asset/HullPackage.h"
#include "nlhc/fleet/FleetDocument.h"
#include "nlhc/hull/HullAssembler.h"

class QAction;
class QDockWidget;
class QMenu;
class QLabel;
class QLineEdit;
class QListWidget;
class QSortFilterProxyModel;
class QSplitter;

namespace nlhc {

class HullEditorPanel;
class HullViewport;
class ShipListModel;
class StylePanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFleet(const QString &path);
    void selectShipByIndex(int row);
    bool applyLanguageByName(const QString &name);
    void setDebugViewName(const QString &name);
    void setOrbitAngles(float yawDegrees, float pitchDegrees);
    QString debugRenderLegend() const;
    QString debugLayoutSummary() const;
    QString debugAssemblySummary() const;
    QString summarizeAssembly(const nlhc::AssemblyResult &assembly) const;

private:
    void applySplitterRatio();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void retranslateMenus();
    void applyLanguage(i18n::Language language);

    void openFileDialog();
    bool openFile(const QString &path);
    bool saveFile();
    bool saveFileAs();
    void exportImage();
    void showAbout();

    void selectShip(int row);
    void refreshEditor();
    void refreshAssembly();
    void refreshDiagnostics();
    void applyConfig(const RandomHullConfig &config);
    void randomizeCurrent();
    void randomizeFleet();
    void copyToSameType();

    void updateRecentFiles(const QString &path = QString());
    void updateWindowTitle();
    void updateActionsEnabled();
    bool confirmDiscardChanges();
    QString locateAssetsRoot() const;
    int currentShipRow() const;

    HullPackage m_package;
    QString m_packageError;
    FleetDocument m_document;

    ShipListModel *m_shipModel = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    HullViewport *m_viewport = nullptr;
    HullEditorPanel *m_editor = nullptr;
    StylePanel *m_stylePanel = nullptr;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_diagnosticList = nullptr;
    QDockWidget *m_diagnosticDock = nullptr;
    QLabel *m_statusLabel = nullptr;

    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_randomizeAction = nullptr;
    QAction *m_randomizeFleetAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_repairAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_languageMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QList<QAction *> m_debugActions;
    QMenu *m_recentMenu = nullptr;
    QList<QAction *> m_languageActions;
    QSplitter *m_splitter = nullptr;

    QStringList m_recentFiles;
    int m_currentRow = -1;
    AssemblyResult m_lastAssembly;
    QString m_lastAssemblySummary;
    bool m_handlingLanguageChange = false;
    bool m_splitterSized = false;
    bool m_userAdjustedSplitter = false;
};

} // namespace nlhc
