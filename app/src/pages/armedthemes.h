#pragma once

#include "model/theme.h"
#include "render/lighting.h"
#include "ui/page.h"

#include <QElapsedTimer>
#include <QTimer>

#include <memory>

class BracketFrame;
class ColorButton;
class DirectionDial;
class LayerList;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class Segmented;
class ThemeKeyboard;

// Lighting > THEME EDITOR: themes built from layers, edited live on the keyboard
class ThemeEditorPage : public Page {
    Q_OBJECT

public:
    explicit ThemeEditorPage(Controller *ctl);
    void keyboardChanged() override;
    void activate() override;
    void deactivate() override;

    // from the keyboard view
    void keyDown(int key, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void keyDrag(int key);
    void keyUp() { dragAction_ = Drag::None; }
    std::optional<QPointF> centerPoint() const; // in view pixels
    QSet<int> coveredKeys() const;

private:
    enum class Drag { None, Add, Remove, Paint };
    struct Row {
        QList<QWidget *> widgets;
    };

    bool loaded() const;
    theme::Theme &theme();
    theme::Layer *layer();
    int nLeds() const;
    QByteArray pack(int slot) const;

    void loadThemes();
    void changed(bool layers = false, bool controls = false);
    void closeGroup();
    void flushGroup();
    void edit(const std::function<void(theme::Layer &)> &f);
    void editFlag(int flag, bool on);
    void push();
    void applyToKeyboard();

    void onType(int index);
    void onAdd(int type);
    void onDuplicate();
    void onMove(int d);
    void onDelete();
    void onGroup(const QString &g);
    void onPreset(int index);
    void onUndo();
    void onRedo();
    void onSlot(int slot);
    void onSave();
    void onRevert();
    void onExport();
    void onImport();

    void refreshAll();
    void refreshState();
    void refreshLayers();
    void refreshLayerRow();
    void refreshControls();
    void refreshValues();
    bool mixesWithNothing() const;
    void refreshKeys();
    QSet<int> layerKeys(int n) const;
    QSet<int> covered(const theme::Layer &l) const;
    void makeCustom(theme::Layer &l) const;
    QStringList slotLabels() const;
    Row addRow(QGridLayout *grid, int r, QWidget *label, QWidget *widget, QWidget *value = nullptr);
    Row addRow(QGridLayout *grid, int r, QWidget *label, QLayout *layout);
    void showRow(const Row &row, bool on);
    void tick();

    // state
    QList<QByteArray> saved_, sent_;
    QList<QList<theme::Theme>> undo_, redo_;
    QList<theme::Theme> lastState_;
    int slot_ = 0, cur_ = -1;
    bool updating_ = false, pickingCenter_ = false;
    Drag dragAction_ = Drag::None;
    QList<int> ledKeys_;    // LED -> key index
    QHash<int, int> keyLed_; // key index -> LED
    QMap<QString, QSet<int>> groups_;
    mutable QHash<int, QSet<int>> layerKeys_; // keymap layer -> LEDs, until the keymap changes
    QList<int> keysModes_;                    // keysMode_ index -> theme::Layer::keys
    std::unique_ptr<lighting::ThemeRenderer> renderer_;
    QElapsedTimer clock_;
    qint64 lastDemo_ = 0;
    QTimer frameTimer_, pushTimer_, groupTimer_;
    QString error_;

    // widgets
    Segmented *slots_;
    QLineEdit *name_;
    QLabel *state_, *keysHint_, *tools_, *count_, *hint_, *empty_, *mixHint_;
    QPushButton *undoBtn_, *redoBtn_, *saveBtn_, *revertBtn_, *addBtn_, *dupBtn_, *upBtn_, *downBtn_, *delBtn_,
        *swapBtn_, *centerBtn_;
    QList<QPushButton *> groupButtons_;
    QCheckBox *solo_, *caps_, *radial_, *mirror_;
    QComboBox *previewLayer_;
    Segmented *keysMode_;
    std::pair<QSet<int>, std::optional<QPointF>> overlay_;
    ThemeKeyboard *kb_;
    LayerList *layers_;
    BracketFrame *props_;
    QComboBox *type_, *cond_;
    ColorButton *colorA_, *colorB_;
    QLabel *colorsLabel_;
    DirectionDial *dial_;
    Segmented *radialWay_, *blend_;
    QSlider *speed_, *size_, *param_, *opacity_;
    QLabel *speedLabel_, *sizeLabel_, *paramLabel_, *speedVal_, *sizeVal_, *paramVal_, *opacityVal_;
    Row rowKeys_, rowType_, rowColors_, rowDir_, rowSpeed_, rowSize_, rowParam_, rowOpacity_, rowBlend_, rowCond_;
};
