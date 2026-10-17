#pragma once

#include "ui/page.h"

// Every navigation entry the app has, in order; each decides when it applies
QList<NavEntry> navEntries();

// stands in for a page that has not been ported from the Python app yet
class PendingPage : public Page {
    Q_OBJECT

public:
    PendingPage(Controller *ctl, const QString &title);
};
