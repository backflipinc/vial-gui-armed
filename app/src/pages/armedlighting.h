#pragma once

#include "render/lighting.h"
#include "ui/page.h"

#include <QElapsedTimer>
#include <QTimer>

#include <memory>

class BracketFrame;
class KeyboardView;
class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class Segmented;
class StripView;
class Swatches;

// Lighting > EFFECTS for qmk-armed keyboards: every effect with a live preview, colour 2, Fn colour, light bar
class ArmedLightingPage : public Page {
    Q_OBJECT

public:
    explicit ArmedLightingPage(Controller *ctl);
    void keyboardChanged() override;
    void activate() override;
    void deactivate() override;

private:
    void load();
    void sync();
    void tick();
    void labelThemes();
    void set(quint8 vid, int value);
    std::optional<int> themeSlot() const;
    qint64 now() const { return clock_.elapsed(); }

    QListWidget *effects_;
    QPushButton *enabled_;
    QLabel *hint_;
    BracketFrame *colorFrame_, *color2Frame_, *levelFrame_, *fnFrame_, *stripFrame_;
    QLabel *chip_, *color2Name_, *fnName_;
    QSlider *hue_, *sat_, *val_, *speed_;
    Swatches *color2_, *fnColor_, *stripColor_;
    QPushButton *holdFn_;
    Segmented *stripMode_;
    QCheckBox *stripCaps_;
    StripView *stripView_;
    KeyboardView *kb_;

    QTimer timer_;
    QElapsedTimer clock_;
    qint64 lastDemo_ = 0;
    std::unique_ptr<lighting::EffectPreview> preview_;
    std::unique_ptr<lighting::ThemeRenderer> themePreview_;
    lighting::StripPreview strip_;
    QList<int> ledKeys_; // LED -> key index in the view, -1 if none
    QSet<QPair<int, int>> fnKeys_;
    bool updating_ = false;
};
