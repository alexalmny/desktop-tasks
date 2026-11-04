#pragma once
class QWidget;
void initializeDesktopShell(bool desktop);
bool attachToDesktop(QWidget *widget, bool desktop, int x, int y);
void positionDesktop(QWidget *widget, int x, int y);
