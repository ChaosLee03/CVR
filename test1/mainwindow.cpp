#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    resize(1200, 800);
//    setCentralWidget(ui->textEdit);//将MainWindow中的textEdit填充到整个窗口
    setWindowIcon(QIcon(":/new/prefix1/QQ.png"));
    QString rootpath = "/Users/mypc/Desktop";
    dirmodel = new QFileSystemModel();
    dirmodel->setRootPath(rootpath);

    ui->treeView->setModel(dirmodel);
    ui->treeView->resizeColumnToContents(0);
    codeeditor = new CodeEditor(ui->textEdit);
    codeeditor->setMinimumWidth(1000);
    codeeditor->setMinimumHeight(600);
}

MainWindow::~MainWindow()
{
    delete ui;
}





QString str2qstr(const std::string str)
{
    return QString::fromLocal8Bit(str.data());//有时候用QString::fromUtf8(str.data())
}



void MainWindow::Save() {
    //保存文件

    if (filename.isEmpty()) {
        QStringList ltFilePath;
        QFileDialog dialog(this, tr("Open Files"));
        dialog.setAcceptMode(QFileDialog::AcceptOpen);  ///< 打开文件
        dialog.setModal(QFileDialog::ExistingFiles);
        dialog.setOption(QFileDialog::DontUseNativeDialog); ///< 不是用本地对话框
        dialog.exec();
        ltFilePath = dialog.selectedFiles();
        QListIterator<QString> itr(ltFilePath);
        filename = itr.next();
        qDebug() << filename.size() << "\n";
        QString fileName;
        std::string a;
        for (long long  i = 0; i < filename.size(); i++) {
            if (filename[i] == '/') {
                fileName.append("//");
            }
            else {
                fileName.append(filename[i]);
            }
        }
        filename = fileName;
    }
    QFile myfile(filename);
    if (!myfile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误!", "保存文件失败！");
        return;
    }
    QTextStream out(&myfile);
    QString m_Text = ui->textEdit->toPlainText();

    out << m_Text;   // 将获取的textEdit中的内容写入文件

    myfile.close();
}

void MainWindow::Ifneedsave() {
    //检查是否修改过textEdit以及是否需要保存
    QMessageBox::StandardButton reply;
    reply = QMessageBox::information(this, "Yes/Not", "文件已被修改，是否要保存修改后的文件？", QMessageBox::Yes | QMessageBox::No);
        if(reply == QMessageBox::Yes){
            Save();//保存
        }
}

void MainWindow::on_actionNew_triggered()
{
        Ifneedsave();
        ui->textEdit->clear();
}


void MainWindow::on_actionQuit_triggered()
{
    //退出按钮
    MainWindow::~MainWindow();
}


void MainWindow::on_actionOpen_triggered()
{
    //打开文件

//    ui->textEdit->clear();
    codeeditor->clear();
        QStringList ltFilePath;
        QFileDialog dialog(this, tr("Open Files"));
        dialog.setAcceptMode(QFileDialog::AcceptOpen);  ///< 打开文件
        dialog.setModal(QFileDialog::ExistingFiles);
        dialog.setOption(QFileDialog::DontUseNativeDialog); ///< 不是用本地对话框
        dialog.exec();
        ltFilePath = dialog.selectedFiles();
        QListIterator<QString> itr(ltFilePath);
        filename = itr.next();
        qDebug() << filename.size() << "\n";
        QString fileName;
        for (long long  i = 0; i < filename.size(); i++) {
            if (filename[i] == '/') {
                fileName.append("//");
            }
            else {
                fileName.append(filename[i]);
            }
        }
        QFile myFile(fileName);//这里必须使用双下划线!!!

        if(!myFile.open(QIODevice::ReadOnly | QIODevice::Text)){
            QMessageBox::warning(this, "警告", "打开文件失败！");
            filename = "";
            return;
        }
        QTextStream in(&myFile);
        QString m_Text = in.readAll();
        codeeditor->clear();
        codeeditor->setPlainText(m_Text);
        ui->statusbar->showMessage("文件路径："+filename);
        qDebug() << filename << Qt::endl;
        QString folderpath;
        int flag = 0;
        for (int i = filename.size() - 1; i >= 0; i--) {
            if (filename[i] == '/') {
                flag = i;
                continue;
            }
            if (flag != 0) {
                folderpath = filename.mid(0, flag + 1);
                qDebug() << folderpath << Qt::endl;
                break;
            }
        }

}


void MainWindow::on_actionSave_triggered()
{
    //保存文件
        QString fileName;
        if (filename.isEmpty()) {
            QStringList ltFilePath;
            QFileDialog dialog(this, tr("Open Files"));
            dialog.setAcceptMode(QFileDialog::AcceptSave);  ///< 打开文件
            dialog.setModal(QFileDialog::AnyFile);
            dialog.setOption(QFileDialog::DontUseNativeDialog); ///< 不是用本地对话框
            QStringList fit;
            fit << ".c";
            fit << ".cpp";
            fit << ".txt";
            dialog.setNameFilters(fit);
            dialog.exec();
            ltFilePath = dialog.selectedFiles();
            QListIterator<QString> itr(ltFilePath);
            filename = itr.next();
            for (long long  i = 0; i < filename.size(); i++) {
                if (filename[i] == '/') {
                    fileName.append("//");
                }
                else {
                    fileName.append(filename[i]);
                }
            }
            fileName += dialog.selectedNameFilter();
            filename += dialog.selectedNameFilter();
        }
        else {
            for (long long  i = 0; i < filename.size(); i++) {
                if (filename[i] == '/') {
                    fileName.append("//");
                }
                else {
                    fileName.append(filename[i]);
                }
            }
        }
        qDebug() << fileName << "\n";
        QFile myfile(fileName);
        if (!myfile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(0, "警告", "保存文件失败!");
            return;
        }
        QTextStream out(&myfile);
        QString mytext = codeeditor->toPlainText();
        out << mytext;
        QMessageBox::information(0, "提示", "文件保存成功！");
        myfile.close();
}



void MainWindow::on_actionDependenceAnalysis_triggered()
{
    //调用数据依赖、控制依赖分析
        QString fileName = filename;
        qDebug() << fileName << "****adsfasf\n";
        if (fileName.contains(".c")) {
            fileName.replace(".c", ".bc");
        }
        else if (fileName.contains(".cpp")) {
            fileName.replace(".cpp", ".bc");
        }

            MainWindow::on_actionBC_triggered();
        bool ok;
        QString text = QInputDialog::getText(this, tr("依赖分析"),tr("请输入一个程序里的全局变量"), QLineEdit::Normal,0, &ok);
        if (ok && !text.isEmpty())
        {
            QMessageBox::information(0, "成功", "已接受该变量");
        }
        QString dependecenPass = QDir::currentPath() + tr("/llvm-my-dump");
        qDebug() << dependecenPass << "\n";

        QString currentPath;
        int flag = 0;
        for (long long i = 0; i < fileName.size(); i++) {

                currentPath.append(fileName[i]);

        }
        QStringList arguments;
        arguments << "-Varname=" + text << currentPath;
        qDebug() << arguments << "\n";
        QProcess process;
        process.start(dependecenPass, arguments);
        if (!process.waitForFinished()) {
            qWarning() << "Process failed to finish";
        }
        QString output = process.readAllStandardOutput();
        qDebug() << "Output: " << output;
        QByteArray output2 = process.readAllStandardOutput();
        QByteArray error = process.readAllStandardError();

        // 将字节序列转换为字符串并输出
        qDebug() << "Standard Output: " << QString(output2);
        qDebug() << "Standard Error: " << QString(error);
        ui->statusbar->showMessage(output);
        ui->textEdit_2->setPlainText(output);
}


void MainWindow::on_actionShortDistanceAnalysis_triggered()
{
    //较短距离分析
        QString dependecenPass = QDir::currentPath() + tr("/Shortdistancetool");
        qDebug() << dependecenPass << "\n";
        QProcess process;
        QStringList arguments;
        arguments << filename;
        process.start(dependecenPass, arguments);
        if (!process.waitForFinished()) {
            qWarning() << "Process failed to finish";
        }
        QString output = process.readAllStandardOutput();
        qDebug() << "Output: " << output;
        QByteArray output2 = process.readAllStandardOutput();
        QByteArray error = process.readAllStandardError();

        // 将字节序列转换为字符串并输出
        qDebug() << "Standard Output: " << QString(output2);
        qDebug() << "Standard Error: " << QString(error);
        ui->statusbar->showMessage(output);
        ui->textEdit_2->setPlainText(output);
}


void MainWindow::on_actionSimilarAnalysis_triggered()
{
    //相似名称分析
        QString dependecenPass = QDir::currentPath() + tr("/Nametool");
        qDebug() << dependecenPass << "\n";
        QProcess process;
        QStringList arguments;
        arguments << filename;
        process.start(dependecenPass, arguments);
        if (!process.waitForFinished()) {
            qWarning() << "Process failed to finish";
        }
        QString output = process.readAllStandardOutput();
        qDebug() << "Output: " << output;
        QByteArray output2 = process.readAllStandardOutput();
        QByteArray error = process.readAllStandardError();

        // 将字节序列转换为字符串并输出
        qDebug() << "Standard Output: " << QString(output2);
        qDebug() << "Standard Error: " << QString(error);
        ui->statusbar->showMessage(output);
        ui->textEdit_2->setPlainText(output);
}


void MainWindow::on_action_IOAnalysis_triggered()
{
    //连续IO地址分析
        QString dependecenPass = QDir::currentPath() + tr("/Iotool");
        qDebug() << dependecenPass << "\n";
        QProcess process;
        QStringList arguments;
        arguments << filename;
        process.start(dependecenPass, arguments);
        if (!process.waitForFinished()) {
            qWarning() << "Process failed to finish";
        }
        QString output = process.readAllStandardOutput();
        qDebug() << "Output: " << output;
        QByteArray output2 = process.readAllStandardOutput();
        QByteArray error = process.readAllStandardError();

        // 将字节序列转换为字符串并输出
        qDebug() << "Standard Output: " << QString(output2);
        qDebug() << "Standard Error: " << QString(error);
        ui->statusbar->showMessage(output);
        ui->textEdit_2->setPlainText(output);
}


void MainWindow::on_actionBrotherAnalysis_triggered()
{
    //兄弟元素分析
        QString dependecenPass = QDir::currentPath() + tr("/Brothertool");
        qDebug() << dependecenPass << "\n";
        QProcess process;
        QStringList arguments;
        arguments << filename;
        process.start(dependecenPass, arguments);
        if (!process.waitForFinished()) {
            qWarning() << "Process failed to finish";
        }
        QString output = process.readAllStandardOutput();
        qDebug() << "Output: " << output;
        QByteArray output2 = process.readAllStandardOutput();
        QByteArray error = process.readAllStandardError();

        // 将字节序列转换为字符串并输出
        qDebug() << "Standard Output: " << QString(output2);
        qDebug() << "Standard Error: " << QString(error);
        ui->statusbar->showMessage(output);
        ui->textEdit_2->setPlainText(output);
}


void MainWindow::on_actionAll_triggered()
{
    //综合分析
}


void MainWindow::on_actionSaveSomewhere_triggered()
{
    //另存为
        QStringList ltFilePath;
        QFileDialog dialog(this, tr("Open Files"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);  ///< 打开文件
        dialog.setModal(QFileDialog::AnyFile);
        dialog.setOption(QFileDialog::DontUseNativeDialog); ///< 不是用本地对话框
        QStringList fit;
        fit << ".c";
        fit << ".cpp";
        fit << ".txt";
        dialog.setNameFilters(fit);
        dialog.exec();
        ltFilePath = dialog.selectedFiles();
        QListIterator<QString> itr(ltFilePath);
        filename = itr.next();
        QString fileName;
        for (long long  i = 0; i < filename.size(); i++) {
            if (filename[i] == '/') {
                fileName.append("//");
            }
            else {
                fileName.append(filename[i]);
            }
        }
        filename = fileName;
        filename += dialog.selectedNameFilter();
        QFile myfile(filename);
        if (!myfile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(0, "警告", "保存文件失败!");
            return;
        }
        QTextStream out(&myfile);
        QString mytext = ui->textEdit->toPlainText();

        ui->statusbar->showMessage("文件保存成功！");
        myfile.close();
}


void MainWindow::on_actionBC_triggered()
{
    //生成bc文件
        if (filename.isEmpty()) {
            QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
            return;
        }
        if (!filename.contains(".c") && !filename.contains(".cpp")) {
            QMessageBox::information(0, "警告", "不是c文件，无法进行生成!");
            return;
        }
        QProcess process;
        QString currentPath;
        int flag = 0;
        for (long long i = 0; i < filename.size(); i++) {

            currentPath.append(filename[i]);

        }
        QString targetPath = currentPath;
        if (currentPath.contains(".c")) {
            targetPath.replace(".c", ".bc");
        }
        else if (currentPath.contains(".cpp")) {
            targetPath.replace(".cpp", ".bc");
        }
        if (headfileloc.isEmpty()) {
            QMessageBox::information(0, "警告", "未设置C库头文件路径!");
            return;
        }
        QStringList arguments;
        arguments << "-emit-llvm" << "-c" << currentPath << "-I" << headfileloc << "-o" << targetPath;
        qDebug() << arguments << "\n";
        QString command = "clang " + arguments.join(" ");
        qDebug() << command;
        process.start("clang", arguments);
        process.waitForFinished();

        QByteArray output = process.readAllStandardOutput();
        QByteArray error = process.readAllStandardError();

        // 将字节序列转换为字符串并输出
        qDebug() << "Standard Output: " << QString(output);
        qDebug() << "Standard Error: " << QString(error);
        QMessageBox::information(0, "提示", "已生成BC文件");
}


void MainWindow::on_action_IR_triggered()
{
    //生成IR文件
        if (filename.isEmpty()) {
            QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
            return;
        }
        if (!filename.contains(".c") && !filename.contains(".cpp")) {
            QMessageBox::information(0, "警告", "不是c文件，无法进行生成!");
            return;
        }
        QProcess process;
        QString currentPath;
        int flag = 0;
        for (long long i = 0; i < filename.size(); i++) {
                currentPath.append(filename[i]);
        }

        QString targetPath = currentPath;
        if (currentPath.contains(".c")) {
            targetPath.replace(".c", ".ll");
        }
        else if (currentPath.contains(".cpp")) {
            targetPath.replace(".cpp", ".ll");
        }
        if (headfileloc.isEmpty()) {
            QMessageBox::information(0, "警告", "未设置C库头文件路径!");
            return;
        }
        QStringList arguments;
        arguments << "-emit-llvm" << "-S" << currentPath << "-I" << headfileloc << "-g" << "-o" << targetPath;
        qDebug() << arguments << "\n";
        QString command = "clang " + arguments.join(" ");
        qDebug() << command;  // 输出 "clang -emit-llvm -S hello.c -o hello.ll"
        process.start("clang", arguments);
        process.waitForFinished();

        QByteArray output = process.readAllStandardOutput();
        QByteArray error = process.readAllStandardError();

        // 将字节序列转换为字符串并输出
        qDebug() << "Standard Output: " << QString(output);
        qDebug() << "Standard Error: " << QString(error);
        QMessageBox::information(0, "提示", "已生成IR文件");

}


void MainWindow::on_actionHeadFiles_triggered()
{
    //配置头文件路径
bool ok;
    QString text = QInputDialog::getText(this, tr("C库头文件路径配置"),tr("请输入头文件绝对地址"), QLineEdit::Normal,0, &ok);
    if (ok && !text.isEmpty())
    {
            headfileloc = text;
            QMessageBox::information(0, "成功", "配置成功！");
    }

}


void MainWindow::on_treeView_clicked(const QModelIndex &index)
{
    //点击文件目录树
    QString spath = dirmodel->fileInfo(index).absoluteFilePath();
    if (spath.contains(".c") || spath.contains(".cpp") || spath.contains(".txt")) {
            filename = spath;
        QString fileName;
//        std::string a;
        for (long long  i = 0; i < filename.size(); i++) {
            if (filename[i] == '/') {
                fileName.append("//");
            }
            else {
                fileName.append(filename[i]);
            }
        }
        QFile myFile(fileName);//这里必须使用双下划线!!!

        if(!myFile.open(QIODevice::ReadOnly | QIODevice::Text)){
            QMessageBox::warning(this, "警告", "打开文件失败！");
            filename = "";
            return;
        }

        QTextStream in(&myFile);

        QString m_Text = in.readAll();

//        ui->textEdit->clear();
        codeeditor->clear();

//        ui->textEdit->setPlainText(m_Text);
        codeeditor->setPlainText(m_Text);
        ui->statusbar->showMessage("文件路径："+filename);
        qDebug() << filename << Qt::endl;
    }
    else {

    }
}


void MainWindow::on_actionLLVMStyle_triggered()
{
    //LLVM风格
    if (filename.isEmpty()) {
        QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
        on_actionSave_triggered();
        return;
    }
    makestyle(1);

}


void MainWindow::on_actionGoogleStyle_triggered()
{
    //Google风格
    if (filename.isEmpty()) {
        QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
        on_actionSave_triggered();
        return;
    }
    makestyle(2);
}


void MainWindow::on_actionChromiumStyle_triggered()
{
    //Chromium风格
    if (filename.isEmpty()) {
        QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
        on_actionSave_triggered();
        return;
    }
    makestyle(3);
}


void MainWindow::on_actionMozillaStyle_triggered()
{
    //MozillaS风格
    if (filename.isEmpty()) {
        QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
        on_actionSave_triggered();
        return;
    }
    makestyle(4);
}


void MainWindow::on_actionWebKitStyle_triggered()
{
    //WebKit风格
    if (filename.isEmpty()) {
        QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
        on_actionSave_triggered();
        return;
    }
    makestyle(5);
}


void MainWindow::on_actionMicrosoftStyle_triggered()
{
    //Microsoft风格
    if (filename.isEmpty()) {
        QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
        on_actionSave_triggered();
        return;
    }
    makestyle(6);
}


void MainWindow::on_actionGNUStyle_triggered()
{
    //GNU风格
    if (filename.isEmpty()) {
        QMessageBox::warning(0, "警告", "没有选择文件或未保存文件!");
        on_actionSave_triggered();
        qDebug() << filename << "\n";
        return;
    }
    makestyle(7);
}

void MainWindow::makestyle(int style) {
    QStringList arguments;
    if (filename.isEmpty()) {
        QMessageBox::warning(0, "警告", "失败");
        return;
    }
    if (style == 1) {
        arguments << "-style=LLVM" << filename;
    }
    else if (style == 2) {
        arguments << "-style=Google" << filename;
    }
    else if (style == 3) {
        arguments << "-style=Chromium" << filename;
    }
    else if (style == 4) {
        arguments << "-style=Mozilla" << filename;
    }
    else if (style == 5) {
        arguments << "-style=WebKit" << filename;
    }
    else if (style == 6) {
        arguments << "-style=Microsoft" << filename;
    }
    else {
        arguments << "-style=GNU" << filename;
    }
    QString command = "clang-format " + arguments.join(" ");
    qDebug() << command << "\n";
    QProcess process;
    QString dependecenPass = QDir::currentPath() + tr("/clang-format");
    QString fileDir = QFileInfo(filename).absoluteDir().path();
    qDebug() << fileDir << "\n";
    process.start(dependecenPass, arguments);
    process.waitForFinished();
    QString output = process.readAllStandardOutput();
    QByteArray error = process.readAllStandardError();

    // 将字节序列转换为字符串并输出
    qDebug() << "Standard Output: " << output;
    qDebug() << "Standard Error: " << QString(error);
    codeeditor->clear();
    codeeditor->setPlainText(output);
}
