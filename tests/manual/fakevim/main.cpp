
#include "fakevimhandler.h"

#include <QtCore/QDebug>

#include <QtGui/QApplication>
#include <QtGui/QMainWindow>
#include <QtGui/QMessageBox>
#include <QtGui/QPlainTextEdit>
#include <QtGui/QStatusBar>
#include <QtGui/QTextEdit>

using namespace FakeVim::Internal;

class Proxy : public QObject
{
    Q_OBJECT

public:
    Proxy(QWidget *widget, QMainWindow *mw, QObject *parent = 0)
      : QObject(parent), m_widget(widget), m_mainWindow(mw)
    {}

public slots:
    void changeSelection(const QList<QTextEdit::ExtraSelection> &s)
    {
        if (QPlainTextEdit *ed = qobject_cast<QPlainTextEdit *>(m_widget))
            ed->setExtraSelections(s);
        else if (QTextEdit *ed = qobject_cast<QTextEdit *>(m_widget))
            ed->setExtraSelections(s);
    }

    void changeStatusData(const QString &info)
    {
        m_statusData = info;
        updateStatusBar();
    }

    void changeStatusMessage(const QString &info)
    {
        m_statusMessage = info;
        updateStatusBar();
    }

    void changeExtraInformation(const QString &info)
    {
        QMessageBox::information(m_widget, "Information", info);
    }
    
    void updateStatusBar()
    {
        int slack = 80 - m_statusMessage.size() - m_statusData.size();
        QString msg = m_statusMessage + QString(slack, QChar(' ')) + m_statusData;
        m_mainWindow->statusBar()->showMessage(msg);
    }

private:
    QWidget *m_widget;
    QMainWindow *m_mainWindow;
    QString m_statusMessage;
    QString m_statusData;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QStringList args = app.arguments();
    (void) args.takeFirst();

    QWidget *widget = 0;
    QString title;
    bool usePlainTextEdit = args.size() < 2;
    if (usePlainTextEdit) {
        QPlainTextEdit *w = new QPlainTextEdit;
        w->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        title = "PlainTextEdit";
        widget = w;
    } else {
        QTextEdit *w = new QTextEdit;
        w->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        title = "TextEdit";
        widget = w;
    }
    widget->setObjectName("Editor");
    //widget->resize(450, 350);
    widget->setFocus();

    QMainWindow mw;
    Proxy proxy(widget, &mw);

    FakeVimHandler handler(widget, 0);

    mw.setWindowTitle("Fakevim (" + title + ")");
    mw.setCentralWidget(widget);
    mw.resize(600, 650);
    mw.move(0, 0);
    mw.show();
    
    QFont font = widget->font();
    //: -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1
    //font.setFamily("Misc");
    font.setFamily("Monospace");
    //font.setStretch(QFont::SemiCondensed);

    widget->setFont(font);
    mw.statusBar()->setFont(font);

    QObject::connect(&handler, SIGNAL(commandBufferChanged(QString)),
        &proxy, SLOT(changeStatusMessage(QString)));
    //QObject::connect(&handler, SIGNAL(quitRequested(bool)),
    //    &app, SLOT(quit()));
    QObject::connect(&handler,
        SIGNAL(selectionChanged(QList<QTextEdit::ExtraSelection>)),
        &proxy, SLOT(changeSelection(QList<QTextEdit::ExtraSelection>)));
    QObject::connect(&handler, SIGNAL(extraInformationChanged(QString)),
        &proxy, SLOT(changeExtraInformation(QString)));
    QObject::connect(&handler, SIGNAL(statusDataChanged(QString)),
        &proxy, SLOT(changeStatusData(QString)));

    theFakeVimSetting(ConfigUseFakeVim)->setValue(true);
    theFakeVimSetting(ConfigShiftWidth)->setValue(8);
    theFakeVimSetting(ConfigTabStop)->setValue(8);
    theFakeVimSetting(ConfigAutoIndent)->setValue(true);

    handler.installEventFilter();
    handler.setupWidget();
    if (args.size() >= 1)
        handler.handleCommand("r " + args.at(0));

    return app.exec();
}

#include "main.moc"
