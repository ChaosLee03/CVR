#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "QtWidgets/qtreewidget.h"
#include <QMainWindow>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <qinputdialog.h>
#include <QProcess>
#include <iostream>
#include <QFileSystemModel>



QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_actionNew_triggered();

    void on_actionQuit_triggered();

    void on_actionOpen_triggered();

    void on_actionSave_triggered();

    void on_actionDependenceAnalysis_triggered();

    void on_actionShortDistanceAnalysis_triggered();

    void on_actionSimilarAnalysis_triggered();

    void on_action_IOAnalysis_triggered();

    void on_actionBrotherAnalysis_triggered();

    void on_actionAll_triggered();

    void Save();

    void Ifneedsave();

    void on_actionSaveSomewhere_triggered();

    void on_actionBC_triggered();

    void on_action_IR_triggered();

    void on_actionHeadFiles_triggered();

    void on_treeView_clicked(const QModelIndex &index);

private:
    Ui::MainWindow *ui;
    QString filename;//文件的路径加名称

    QString headfileloc = tr("/Library/Developer/CommandLineTools/SDKs/MacOSX13.0.sdk/usr/include");//头文件的地址
    QFileSystemModel *dirmodel;
};
#endif // MAINWINDOW_H
