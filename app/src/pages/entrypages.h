#pragma once

#include "ui/page.h"

class QListWidget;
class QScrollArea;
class QLabel;

// Numbered entries on the left with what each one does, the selected one's editor on the right.
// Changes go to the keyboard as they are made.
class EntryPage : public Page {
    Q_OBJECT

public:
    explicit EntryPage(Controller *ctl, const QString &hint);
    void keyboardChanged() override;

protected:
    virtual int kind() const = 0; // EntryKinds
    virtual int count() const = 0;
    virtual QString summary(int index) const = 0;
    virtual QWidget *editor(int index) = 0;
    void refreshSummary(int index);

private:
    void populate();
    void show(int index);

    QListWidget *list_;
    QScrollArea *area_;
    QLabel *hint_;
};

class TapDancePage : public EntryPage {
    Q_OBJECT
public:
    explicit TapDancePage(Controller *ctl);
protected:
    int kind() const override { return TapDanceEntries; }
    int count() const override;
    QString summary(int index) const override;
    QWidget *editor(int index) override;
};

class ComboPage : public EntryPage {
    Q_OBJECT
public:
    explicit ComboPage(Controller *ctl);
protected:
    int kind() const override { return ComboEntries; }
    int count() const override;
    QString summary(int index) const override;
    QWidget *editor(int index) override;
};

class KeyOverridePage : public EntryPage {
    Q_OBJECT
public:
    explicit KeyOverridePage(Controller *ctl);
protected:
    int kind() const override { return KeyOverrideEntries; }
    int count() const override;
    QString summary(int index) const override;
    QWidget *editor(int index) override;
};

class AltRepeatKeyPage : public EntryPage {
    Q_OBJECT
public:
    explicit AltRepeatKeyPage(Controller *ctl);
protected:
    int kind() const override { return AltRepeatKeyEntries; }
    int count() const override;
    QString summary(int index) const override;
    QWidget *editor(int index) override;
};
