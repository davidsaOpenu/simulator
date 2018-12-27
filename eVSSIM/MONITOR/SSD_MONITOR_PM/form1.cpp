
#include <QFile>
#include <QFileDialog>

#include <qvariant.h>
#include <qimage.h>
#include <qpixmap.h>

#include <stdlib.h>

#include "form1.h"
#include "ui_form1.h"

#include "ssd_log_monitor.h"

Form1* Form1::Instance = NULL;
pthread_mutex_t Form1::InstanceLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t Form1::InstanceCondition = PTHREAD_COND_INITIALIZER;

/*
 *  Constructs a Form1 as a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'.
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
Form1::Form1(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Form1)
{
    /* initialize variables. */
    init_var();
    ui->setupUi(this);
    printf("INIT SSD_MONITOR!!!\n");

    /* initialize timer. */
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(onTimer()));
    timer->start(1);

    /* initialize socket connected with VSSIM */
    sock = new QTcpSocket(this);
    connect(sock, SIGNAL(readyRead()), this, SLOT(onReceive()));
    connect(sock, SIGNAL(connectionClosed()), this, SLOT(accept()));
    sock->connectToHost("127.0.0.1", 9999);
}

/*
 *  Destroys the object and frees any allocated resources
 */
Form1::~Form1()
{
    delete ui;
}

/*
 * initialize variables.
 */
void Form1::init_var()
{
    this->setWindowTitle("SSD Monitor");

    INIT_LOGGER_MONITOR(&monitor);
    fflush(stdout);
}

void Form1::onTimer()
{
    char sz_timer[20];
    TimeProg++;
    sprintf(sz_timer, "%lld", TimeProg);
    ui->txtTimeProg->setText(sz_timer);
}

void Form1::printStats()
{
    char szTemp[128];
    // Write
    sprintf(szTemp, "%ld", monitor.write_sector_count);
    ui->txtWriteSectorCount->setText(szTemp);

    sprintf(szTemp, "%ld", monitor.write_count);
	ui->txtWriteCount->setText(szTemp);

	sprintf(szTemp, "%lf", monitor.write_speed);
	ui->txtWriteSpeed->setText(szTemp);

	// Read
	sprintf(szTemp, "%ld", monitor.read_sector_count);
	ui->txtReadSectorCount->setText(szTemp);

	sprintf(szTemp, "%ld", monitor.read_count);
	ui->txtReadCount->setText(szTemp);

	sprintf(szTemp, "%lf", monitor.read_speed);
	ui->txtReadSpeed->setText(szTemp);

	sprintf(szTemp, "%ld", monitor.erase_count);
	ui->txtEraseCount->setText(szTemp);

	// GC
	sprintf(szTemp, "%ld", monitor.gc_count);
	ui->txtGarbageCollectionNB->setText(szTemp);

	// Trim
	sprintf(szTemp, "%d", monitor.trim_count);
	ui->txtTrimCount->setText(szTemp);

	sprintf(szTemp, "%ld", monitor.trim_effect);
    ui->txtTrimEffect->setText(szTemp);

    // Written page
	sprintf(szTemp, "%ld", monitor.written_page);
	ui->txtWrittenBlockCount->setText(szTemp);

    // Write amplification
	sprintf(szTemp, "%0.3f", monitor.write_amplification);
    ui->txtWriteAmpCount->setText(szTemp);

    // SSD Util
	sprintf(szTemp, "%0.3f", monitor.utils);
    ui->txtSsdUtil->setText(szTemp);
}

/*
 * callback method for socket.
 */
void Form1::onReceive()
{
    QStringList szCmdList;
    QString szCmd;
    char szTemp[128];

    while(sock->canReadLine())
    {
        szCmd = sock->readLine();
        szCmdList = szCmd.split(" ");
        // Parse log
        PARSE_LOG(szCmd.toStdString().c_str(), &monitor);
        printStats();
        // Debug
        ui->txtDebug->setText(szCmd.toStdString().c_str());
    } // end while
}


/*
 * callback method for reset button click.
 */
void Form1::btnInit_clicked()
{
    init_var();
    printStats();
    ui->txtDebug->setText("");
}


/*
 * callback method for save button click.
 */
void Form1::btnSave_clicked()
{

    QString fileName;
    fileName = QFileDialog::getSaveFileName(this, tr("Save file"), tr("./data"), tr("Data files (*.dat"));
    QFile file(fileName);
    file.open(QIODevice::WriteOnly);

    QTextStream out(&file);

    char s_timer[20];
    sprintf(s_timer, "%lld", TimeProg);

    /*
    out << "==========================================================================\n";
    out << "|                                                                        |\n";
    out << "|           VSSIM : Virtual SSD Simulator                                |\n";
    out << "|                                                                        |\n";
    out << "|                                   Embedded Software Systems Lab.       |\n";
    out << "|                        Dept. of Electronics and Computer Engineering   |\n";
    out << "|                                         Hanynag Univ, Seoul, Korea     |\n";
    out << "|                                                                        |\n";
    out << "==========================================================================\n\n";
*/

    out << "Write Request\t" << monitor.write_count << "\n";
    out << "Write Sector\t" << monitor.write_sector_count << "\n\n";
    out << "Read Request\t" << monitor.read_count << "\n";
    out << "Read Sector\t" << monitor.read_sector_count << "\n\n";

    out << "Garbage Collection\n";
    out << "  Count\t" << GC_NB <<"\n";
    out << "  Sector Count\t" << GCStarted << "\n\n";

    out << "TRIM Count\t" << monitor.trim_count << "\n";
    out << "TRIM effect\t" << monitor.trim_effect << "\n\n";

    out << "Write Amplification\t" << monitor.write_amplification << "\n";
    out << "Written Page\t" << monitor.written_page << "\n";
    out << "Run-time[ms]\t" << s_timer << "\n";

    file.close();
}


/**
 * Update the statistics displayed in the monitor
 * @param stats the new statistics to display in the monitor
 */
void Form1::updateStats(SSDStatistics stats) {
    char szTemp[128];

    monitor.written_page
		= monitor.write_sector_count
		= monitor.write_count = stats.write_count;

    monitor.read_sector_count = monitor.read_count = stats.read_count;
    monitor.gc_count = stats.garbage_collection_count;
    monitor.write_amplification = stats.write_amplification;
    monitor.utils = stats.utilization * 100.0;

    printStats();
}

/**
 * A hook which is called when the window is shown for the first time
 */
void Form1::showEvent(QShowEvent*) {
    pthread_mutex_lock(&InstanceLock);
    Instance = this;
    pthread_cond_broadcast(&InstanceCondition);
    pthread_mutex_unlock(&InstanceLock);
}

/**
 * Override of the main event handler
 * @param event the event to process
 * @return whether we handled the event or not
 */
bool Form1::event(QEvent* event) {
    if(event->type() == StatsUpdateEvent::type) {
        StatsUpdateEvent* updateEvent = static_cast<StatsUpdateEvent *>(event);
        updateStats(updateEvent->stats);
        return true;
    }

    return QDialog::event(event);
}

