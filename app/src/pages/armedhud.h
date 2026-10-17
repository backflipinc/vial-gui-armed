#pragma once

#include "agent/hudagent.h"
#include "model/hud.h"
#include "protocol/armed.h"
#include "render/hudrender.h"
#include "ui/page.h"

#include <QTimer>

#include <memory>
#include <optional>

class AgentHost;
class BracketFrame;
class HudCanvas;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QShortcut;
class QSpinBox;
class QStackedWidget;
class Segmented;
class TestGui;

// Colour roles as swatches in the keyboard's colours; AUTO first when allowed
class RoleSwatches : public QWidget {
    Q_OBJECT
public:
    explicit RoleSwatches(const QList<int> &roles, QWidget *parent = nullptr);
    void setPalette(const QList<hud::Hsv> &palette);
    void setCurrent(std::optional<int> role);

signals:
    void changed(int role);

private:
    QMap<int, QPushButton *> buttons_;
};

// What the editor's preview shows: the agent's values and the keyboard's, polled while the page is open
class PreviewSources : public hud::Sources {
public:
    explicit PreviewSources(agent::HudAgent &agent)
        : agent_(agent)
    {
    }
    std::optional<hud::Value> value(int source) const override;
    std::optional<QString> hostText(int slot) const override;
    QList<hud::Value> history(int source) const override;

    QHash<int, hud::Value> kb;
    QHash<int, QList<hud::Value>> kbHistory;

private:
    agent::HudAgent &agent_;
};

// Screen > HUD EDITOR: the keyboard's main screen built from widgets on a pixel-exact preview (LAYOUT),
// and what this computer feeds it (AGENT)
class HudEditorPage : public Page {
    Q_OBJECT

public:
    explicit HudEditorPage(Controller *ctl);
    ~HudEditorPage() override;
    void keyboardChanged() override;
    void activate() override;
    void deactivate() override;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    friend class TestGui;
    struct Row {
        QLabel *label;
        QWidget *widget;
    };
    struct ValueRow {
        QComboBox *metric;
        QLineEdit *command;
        QDoubleSpinBox *interval;
        QSpinBox *dec;
        QLabel *live, *used;
    };
    struct TextRow {
        QLineEdit *command;
        QDoubleSpinBox *interval;
        QLabel *live, *used;
    };

    QWidget *buildLayoutView();
    void buildProps();
    QWidget *buildAgentView();

    // device
    bool valid() const;
    void load();
    void loadFonts(const QList<QByteArray> &heads);
    void setFonts(const QList<QByteArray> &fonts);
    QString fontCache(const QByteArray &head) const;
    void showError(const QString &e);
    // stored layouts: the one the keyboard shows is the one edited
    void onScreen(int screen);
    void readLayout(int screen);
    void checkScreen();
    void showLoading(const QString &text, int pct = -1);
    void showCanvas();

    // rendering
    void redraw();
    void refreshLive();
    void poll();
    void fitCanvas();

    // selection
    const hud::Widget *current() const;
    QList<int> validSel() const;
    void setSelection(const QList<int> &sel, bool fromCanvas = false);
    void selectAll();
    int rowOf(int i) const { return int(hud_.widgets.size()) - 1 - i; }
    void syncListSelection();
    void onListSelection();
    void onRowsMoved();
    void onActivate(int i);

    // editing
    void snapshot();
    void changed(bool refreshProps = true);
    void refreshAll();
    void edit(const std::function<void(hud::Widget &)> &fn, bool refreshProps = false);
    void editFlags(int mask, int value);
    int sourceDec(const hud::Widget &wd) const;
    void editScaled(int hud::Widget::*field, double v);
    void onType();
    void onSource();
    void insertValue();
    QPoint freeSpot(int w, int h) const;
    void onAdd(int type);
    void onDuplicate();
    void onCopy();
    void onPaste();
    void onOrder(int d);
    void onDelete();
    void onNudge(int dx, int dy);
    void reorder(const QList<int> &order);

    // arrange
    QList<std::pair<int, hud::Box>> boxes() const;
    void align(const QString &how);
    void distribute(bool x);
    void match(bool width);
    void onContext(const QPoint &global);

    void onUndo();
    void onRedo();
    void onPreset(int index);
    void onImport();
    void onExport();
    void push();
    void onSave();
    void onRevert();

    // panels
    QString summary(const hud::Widget &wd) const;
    QString sourceName(int s) const;
    void refreshList();
    void refreshButtons();
    void refreshStatus();
    void refreshState();
    void showRows(const QSet<QString> &keys);
    void refreshProps();
    void fillSingle(const hud::Widget &wd);
    static bool fits(int type, const armed::HudSource &s);

    // agent
    void loadAgentRows();
    void onValueCfg(int i, const QString &what);
    void onTextCfg(int i);
    void saveConfig();
    void refreshAgent();
    void onAutostart(bool on);

    AgentHost *host_;
    agent::Config cfg_;
    PreviewSources preview_;
    std::optional<armed::HudInfo> info_;
    QList<armed::HudSource> sources_;
    QList<hud::Hsv> palette_;
    QList<std::shared_ptr<hud::QffFont>> fonts_;
    hud::Layout hud_;
    QList<std::optional<hud::Box>> areas_;
    std::optional<hud::Layout> saved_;
    // what the keyboard shows; touched only on the device thread, where every HUD job runs in order
    std::shared_ptr<std::optional<QByteArray>> shown_;
    bool loaded_ = false, loading_ = false, fontsBusy_ = false, polling_ = false, active_ = false;
    QList<int> sel_;
    QList<hud::Layout> undo_, redo_;
    bool updating_ = false;
    int pollIdx_ = 0, fontTries_ = 0, screenTicks_ = 0;
    bool checking_ = false, switching_ = false;
    quint64 serial_ = 0;
    QPoint cursor_{-1, -1};
    QList<QShortcut *> shortcuts_;

    QTimer pushTimer_, pollTimer_, paintTimer_, cfgTimer_;

    // top bar
    Segmented *view_, *screens_;
    QLabel *state_, *error_;
    QPushButton *undoBtn_, *redoBtn_, *presetsBtn_, *importBtn_, *exportBtn_, *revertBtn_, *saveBtn_;
    QStackedWidget *stack_;

    // layout view
    BracketFrame *screenFrame_;
    QLabel *screenLbl_, *status_, *count_;
    Segmented *zoom_;
    QList<QPushButton *> addButtons_;
    QList<std::pair<QString, QPushButton *>> arrangeButtons_;
    QCheckBox *snap_, *bounds_;
    HudCanvas *canvas_;
    QScrollArea *canvasScroll_;
    QStackedWidget *canvasStack_;
    QLabel *loadingLbl_;
    QProgressBar *loadingBar_;
    QListWidget *list_;
    QPushButton *dupBtn_, *upBtn_, *downBtn_, *delBtn_;

    // properties
    BracketFrame *props_;
    QLabel *selLbl_, *hintLbl_, *nowLbl_;
    QMap<QString, Row> rows_;
    QList<std::pair<QLabel *, QStringList>> sections_;
    QComboBox *typeCombo_, *sourceCombo_, *fontCombo_, *frameStyle_, *below_;
    QLineEdit *labelEdit_;
    QPushButton *valueBtn_;
    QSpinBox *digits_, *corner_, *segments_, *gap_;
    QMap<QString, QSpinBox *> spin_;
    RoleSwatches *fg_, *bg_;
    Segmented *alignSeg_;
    QCheckBox *dot_, *fill_, *tail_, *warnChk_;
    QDoubleSpinBox *period_, *lo_, *hi_, *warn_;

    // agent view
    QLabel *agentLbl_;
    QCheckBox *autostart_;
    QList<ValueRow> valueRows_;
    QList<TextRow> textRows_;
};
