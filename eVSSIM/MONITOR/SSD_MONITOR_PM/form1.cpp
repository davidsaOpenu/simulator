
#include <QFile>
#include <QFileDialog>

#include <qvariant.h>
#include <qimage.h>
#include <qpixmap.h>

#include "form1.h"
#include "ui_form1.h"


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

    WriteSecCount = 0;
    ReadSecCount = 0;
    WriteCount = 0;
    ReadCount = 0;


    RandMergeCount = 0;
    SeqMergeCount = 0;

    WriteSecCount = 0;
    TrimEffect = 0;
    TrimCount = 0;
    WrittenCorrectCount = 0;
    WriteAmpCount = 0;
    GCStarted = 0;
    GC_NB = 0;
    TimeProg = 0;
    WriteTime = 0;
    ReadTime = 0;

    fflush(stdout);
}

void Form1::onTimer()
{
    char sz_timer[20];
    TimeProg++;
    sprintf(sz_timer, "%lld", TimeProg);
    ui->txtTimeProg->setText(sz_timer);
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

        /* WRITE : write count, write sector count, write speed. */
        if(szCmdList[0] == "WRITE")
        {
            QTextStream stream(&szCmdList[2]);
            if(szCmdList[1] == "PAGE"){        
                unsigned int length;
                stream >> length;
                // Write Sector Number Count
                WriteSecCount += length;

                sprintf(szTemp, "%ld", WriteSecCount);
                ui->txtWriteSectorCount->setText(szTemp);

                // Write SATA Command Count
                WriteCount++;  
                sprintf(szTemp, "%ld", WriteCount);
                ui->txtWriteCount->setText(szTemp);
            }
            else if(szCmdList[1] == "BW"){
                double time;
                stream >>time;
                if(time != 0){
                    sprintf(szTemp, "%0.3lf", time);
                    ui->txtWriteSpeed->setText(szTemp);
                }
            }
          }        
        /* READ : read count, read sector count, read speed. */
        else if(szCmdList[0] == "READ")
        {
            QTextStream stream(&szCmdList[2]);
            if(szCmdList[1] == "PAGE"){
                unsigned int length;
                  
                /* Read Sector Number Count */
                stream >> length;
                  ReadSecCount += length;

                  sprintf(szTemp, "%ld", ReadSecCount);
                  ui->txtReadSectorCount->setText(szTemp);

                /* Read SATA Command Count */
                ReadCount++;
                sprintf(szTemp, "%ld", ReadCount);
                ui->txtReadCount->setText(szTemp);
            }
            else if(szCmdList[1] == "BW"){
                double time;
                  stream >> time;
                  if(time != 0)
                  {
                      sprintf(szTemp, "%0.3lf", time);
                      ui->txtReadSpeed->setText(szTemp);
                }
            }
        }

        /* GC : gc has been occured. */
        else if(szCmdList[0] == "GC")
          {
            GC_NB++;
            sprintf(szTemp, "%ld", GC_NB);
            ui->txtGarbageCollectionNB->setText(szTemp);

        }
        /* WB : written block count, write amplification. */
        else if(szCmdList[0] == "WB")
            {
            long long int WriteAmpCount = 0;
            QTextStream stream(&szCmdList[2]);
            stream >> WriteAmpCount;

            if(szCmdList[1] == "CORRECT")
            {
                      WrittenCorrectCount += WriteAmpCount;
                      sprintf(szTemp, "%ld", WrittenCorrectCount);
                      ui->txtWrittenBlockCount->setText(szTemp);
            }
            else
            {
                      sprintf(szTemp, "%lld", WriteAmpCount);
                      ui->txtWriteAmpCount->setText(szTemp);
            }
        }    
        /* TRIM : trim count, trim effect. */
        else if(szCmdList[0] == "TRIM")
        {
            if(szCmdList[1] == "INSERT")
            {
                TrimCount++;
                sprintf(szTemp, "%d", TrimCount);
                ui->txtTrimCount->setText(szTemp);        
            }
            else
            {
                int effect = 0;
                QTextStream stream(&szCmdList[2]);
                stream >> effect;
                TrimEffect+= effect;
                sprintf(szTemp, "%d", TrimEffect);
                ui->txtTrimEffect->setText(szTemp);    
            }
        }

        /* UTIL : SSD Util. */
        else if(szCmdList[0] == "UTIL")
        {
            double util = 0;
            QTextStream stream(&szCmdList[1]);
            stream >> util;
            sprintf(szTemp, "%lf", util);
            ui->txtSsdUtil->setText(szTemp);
          }

        ui->txtDebug->setText(szCmd);
    } // end while
}


/*
 * callback method for reset button click.
 */
void Form1::btnInit_clicked()
{
    init_var();

    ui->txtWriteCount->setText("0");
    ui->txtWriteSpeed->setText("0");
    ui->txtWriteSectorCount->setText("0");
    ui->txtReadCount->setText("0");
    ui->txtReadSpeed->setText("0");
    ui->txtReadSectorCount->setText("0");
    ui->txtGarbageCollectionNB->setText("0");
    ui->txtGarbageCollection->setText("0");
    ui->txtGCStart->setText("0"); 
    ui->txtTrimCount->setText("0");
    ui->txtTrimEffect->setText("0");
    ui->txtWrittenBlockCount->setText("0");
    ui->txtWriteAmpCount->setText("0");
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
    
    out << "Write Request\t" << WriteCount << "\n";
    out << "Write Sector\t" << WriteSecCount << "\n\n";
    out << "Read Request\t" << ReadCount << "\n";
    out << "Read Sector\t" << ReadSecCount << "\n\n";

    out << "Garbage Collection\n";
    out << "  Count\t" << GC_NB <<"\n";
    out << "  Sector Count\t" << GCStarted << "\n\n";
    
    out << "TRIM Count\t" << TrimCount << "\n";
    out << "TRIM effect\t" << TrimEffect << "\n\n";
    
    out << "Write Amplification\t" << WriteAmpCount << "\n";
    out << "Written Page\t" << WrittenCorrectCount << "\n";
    out << "Run-time[ms]\t" << s_timer << "\n";

    file.close();
}


/**
 * Update the statistics displayed in the monitor
 * @param stats the new statistics to display in the monitor
 */
void Form1::updateStats(SSDStatistics stats) {
    char szTemp[128];

    WrittenCorrectCount = WriteSecCount = WriteCount = stats.write_count;
    SET_TEXT_I(ui->txtWriteCount, stats.write_count, szTemp);
    ui->txtWriteSectorCount->setText(szTemp);
    ui->txtWrittenBlockCount->setText(szTemp);


    SET_TEXT_F(ui->txtWriteSpeed, stats.write_speed, szTemp);

    ReadSecCount = ReadCount = stats.read_count;
    SET_TEXT_I(ui->txtReadCount, stats.read_count, szTemp);
    ui->txtReadSectorCount->setText(szTemp);

    SET_TEXT_F(ui->txtReadSpeed, stats.read_speed, szTemp);

    GCStarted = GC_NB = stats.garbage_collection_count;
    SET_TEXT_I(ui->txtGarbageCollectionNB, stats.garbage_collection_count, szTemp);
    ui->txtGCStart->setText(szTemp);

    WriteAmpCount = stats.write_amplification;
    SET_TEXT_F(ui->txtWriteAmpCount, stats.write_amplification, szTemp);

    SET_TEXT(ui->txtSsdUtil, "%0.3f%%", stats.utilization * 100.0, szTemp);
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

