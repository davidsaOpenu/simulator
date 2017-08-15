/***************************************************************************
** ui.h extension file, included from the uic-generated form implementation.
**
** If you want to add, delete, or rename functions or slots, use
** Qt Designer to update this file, preserving your code.
**
** You should not define a constructor or destructor in this file.
** Instead, write your code in functions called init() and destroy().
** These will automatically be called by the form's constructor and
** destructor.
*****************************************************************************/
#include "form1.h"
#include "monitor_test.h"
#include <qfile.h>
#include <qfiledialog.h>
#include <qdir.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

#define REG_IS_WRITE 706

/**
 * Set the text of the object
 * @param obj the object to call setText on
 * @param str the string to use for the formatting
 * @param var the variable to pass the formatter
 * @param {char*} buf the buffer to use when formatting
 */
#define SET_TEXT(obj, str, var, buf)    \
    do {                                \
        sprintf(buf, str, var);         \
        obj->setText(buf);              \
    } while(0)
/**
 * Set the text of the object to the integer given
 * @param obj the object to call setText on
 * @param {int} var the integer value
 * @param {char*} buf the buffer to use when formatting
 */
#define SET_TEXT_I(obj, var, buf) SET_TEXT(obj, "%d", var, buf)
/**
 * Set the text of the object to the double given
 * @param obj the object to call setText on
 * @param {int} var the integer value
 * @param {char*} buf the buffer to use when formatting
 */
#define SET_TEXT_F(obj, var, buf) SET_TEXT(obj, "%0.3f", var, buf)

long long int get_usec(void){
	long long int t = 0;
	struct timeval tv;
	struct timezone tz;

	gettimeofday(&tv, &tz);
	t = tv.tv_sec;
	t *= 1000000;
	t += tv.tv_usec;

	return t;
}
void Form1::init_var()
{
	WriteSecCount = 0;
	ReadSecCount = 0;
	WriteCount = 0;
	ReadCount = 0;
	PowCount = 0;

	RandMergeCount = 0;
	SeqMergeCount = 0;
	ExchangeCount = 0;
	WriteSectorCount = 0;
	TrimEffect = 0;
	TrimCount = 0;
	WrittenCorrectCount = 0;
	WriteAmpCount = 0;
	GCStarted = 0;
	GC_NB = 0;
	TimeProg = 0;
	WriteTime = 0;
	ReadTime = 0;
}

void Form1::btnConnect_clicked()
{

    
}


void Form1::init()
{
	printf("INIT SSD_MONITOR!!!\n");
	init_var();

	sock = new QSocket(this);
	connect(sock, SIGNAL(readyRead()), this, SLOT(onReceive()));
	connect(sock, SIGNAL(connectionClosed()), this, SLOT(accept()));

    	/* socket의 서버 설정. 실험 pc의 서버와 동일하게 한다. */
	sock->connectToHost("127.0.0.1", 9999);

	// timer Setting
	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(onTimer()));
	timer->start(1, false);
}

void Form1::onReceive()
{

	QStringList szCmdList;
	QString szCmd;
	char szTemp[128];

	while(sock->canReadLine())
	{

		szCmd = sock->readLine();
	 	szCmdList = QStringList::split(" ", szCmd);

		/* WRITE인 소켓을 받았을 경우 */
		if(szCmdList[0] == "WRITE")
		{
			if(szCmdList[1] == "PAGE"){        
        			unsigned int length;
				sscanf(szCmdList[2], "%u", &length);
				// Write Sector Number Count
				WriteSecCount += length;

				sprintf(szTemp, "%u", WriteSecCount);
				txtWriteSectorCount->setText(szTemp);

				// Write SATA Command Count
				WriteCount++;  
				sprintf(szTemp, "%d", WriteCount);
				txtWriteCount->setText(szTemp);
			}
			else if(szCmdList[1] == "BW"){
				double time;
				sscanf(szCmdList[2], "%lf", &time);
				if(time != 0){
					sprintf(szTemp, "%0.3lf", time);
					txtWriteSpeed->setText(szTemp);
				}
			}
	  	}	    
		/* READ인 소켓을 받았을 경우 */
		else if(szCmdList[0] == "READ")
		{
			if(szCmdList[1] == "PAGE"){
				unsigned int length;
	  			
				/* Read Sector Number Count */
				sscanf(szCmdList[2], "%u", &length);
	  			ReadSecCount += length;

	  			sprintf(szTemp, "%u", ReadSecCount);
	  			txtReadSectorCount->setText(szTemp);

				/* Read SATA Command Count */
				ReadCount++;
				sprintf(szTemp, "%d", ReadCount);
				txtReadCount->setText(szTemp);
			}
			else if(szCmdList[1] == "BW"){
				double time;
	  			sscanf(szCmdList[2], "%lf", &time);
	  			if(time != 0)
	  			{
	  				sprintf(szTemp, "%0.3lf", time);
	  				txtReadSpeed->setText(szTemp);
				}
			}
		}
		/* READF인 소켓을 받았을 경우 */
		else if(szCmdList[0] == "READF")
		{
			int flash_nb;
			sscanf(szCmdList[1], "%d", &flash_nb);
  		}
		/* GC인 소켓을 받았을 경우 */
		else if(szCmdList[0] == "GC")
  		{
			GC_NB++;
			sprintf(szTemp, "%ld", GC_NB);
			txtGarbageCollectionNB->setText(szTemp);

		}
		/* WB인 소켓을 받았을 경우 */
		else if(szCmdList[0] == "WB")
	    	{
			sscanf(szCmdList[2], "%f", &WriteAmpCount);
			if(szCmdList[1] == "CORRECT")
			{
	      			WrittenCorrectCount += WriteAmpCount;
	      			sprintf(szTemp, "%ld", WrittenCorrectCount);
	      			txtWrittenBlockCount->setText(szTemp);
			}
			else
			{
	      			sprintf(szTemp, "%f", WriteAmpCount);
	      			txtWriteAmpCount->setText(szTemp);
			}
		}	
		/* TRIM인 소켓을 받았을 경우 */
		else if(szCmdList[0] == "TRIM")
		{
			if(szCmdList[1] == "INSERT")
			{
				TrimCount++;
				sprintf(szTemp, "%d", TrimCount);
				txtTrimCount->setText(szTemp);	    
			}
			else
			{
				int effect = 0;
				sscanf(szCmdList[2], "%d", &effect);
				TrimEffect+= effect;
				sprintf(szTemp, "%d", TrimEffect);
				txtTrimEffect->setText(szTemp);	
			}
		}
		/* UTIL인 소켓을 받았을 경우 */
		else if(szCmdList[0] == "UTIL")
		{
			double util = 0;
			sscanf(szCmdList[1], "%lf", &util);
			sprintf(szTemp, "%lf", util);
			txtSsdUtil->setText(szTemp);
	  	}

		txtDebug->setText(szCmd);
	} // end while
}


void Form1::btnInit_clicked()
{
    init_var();

    txtWriteCount->setText("0");
    txtWriteSpeed->setText("0");
    txtWriteSectorCount->setText("0");
    txtReadCount->setText("0");
    txtReadSpeed->setText("0");
    txtReadSectorCount->setText("0");
    txtGarbageCollectionNB->setText("0");
    txtGarbageCollection->setText("0");
    txtGCStart->setText("0"); 
    txtTrimCount->setText("0");
    txtTrimEffect->setText("0");
    txtWrittenBlockCount->setText("0");
    txtWriteAmpCount->setText("0");
    txtDebug->setText("");
}


void Form1::btnSave_clicked()
{

    QString fileName;
    fileName = QFileDialog::getSaveFileName("./data" , "Data files (*.dat)", this, "Save file", "Choose a filename to save under");
    QFile file(fileName);
    file.open(IO_WriteOnly);
    
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
    out << "Write Sector\t" << WriteSectorCount << "\n\n";
    out << "Read Request\t" << ReadCount << "\n";
    out << "Read Sector\t" << ReadSecCount << "\n\n";

    out << "Garbage Collection\n";
    out << "  Count\t" << GC_NB <<"\n";
    out << "  Exchange\t" << ExchangeCount << "\n";
    out << "  Sector Count\t" << GCStarted << "\n\n";
    
    out << "TRIM Count\t" << TrimCount << "\n";
    out << "TRIM effect\t" << TrimEffect << "\n\n";
    
    out << "Write Amplification\t" << WriteAmpCount << "\n";
    out << "Written Page\t" << WrittenCorrectCount << "\n";
    out << "Run-time[ms]\t" << s_timer << "\n";

    file.close();
}

void Form1::onTimer()
{
    char sz_timer[20];
    TimeProg++;
    sprintf(sz_timer, "%lld", TimeProg);
    txtTimeProg->setText(sz_timer);
}

/**
 * Update the statistics displayed in the monitor
 * @param stats the new statistics to display in the monitor
 */
void Form1::updateStats(SSDStatistics stats) {
    char szTemp[128];

    WrittenCorrectCount = WriteSecCount = WriteCount = stats.write_count;
    SET_TEXT_I(txtWriteCount, stats.write_count, szTemp);
    txtWriteSectorCount->setText(szTemp);
    txtWrittenBlockCount->setText(szTemp);


    SET_TEXT_F(txtWriteSpeed, stats.write_speed, szTemp);

    ReadSecCount = ReadCount = stats.read_count;
    SET_TEXT_I(txtReadCount, stats.read_count, szTemp);
    txtReadSectorCount->setText(szTemp);

    SET_TEXT_F(txtReadSpeed, stats.read_speed, szTemp);

    GCStarted = GC_NB = stats.garbage_collection_count;
    SET_TEXT_I(txtGarbageCollectionNB, stats.garbage_collection_count, szTemp);
    txtGCStart->setText(szTemp);

    WriteAmpCount = stats.write_amplification;
    SET_TEXT_F(txtWriteAmpCount, stats.write_amplification, szTemp);

    SET_TEXT(txtSsdUtil, "%0.3f%%", stats.utilization * 100.0, szTemp);
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

bool Form1::event(QEvent* event) {
    if(event->type() == StatsUpdateEvent::type) {
        StatsUpdateEvent* updateEvent = static_cast<StatsUpdateEvent *>(event);
        updateStats(updateEvent->stats);
        return true;
    }

    return QDialog::event(event);
}
