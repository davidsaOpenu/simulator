/****************************************************************************
** Form implementation generated from reading ui file 'form1.ui'
**
** Created: Thu Dec 26 07:08:58 2013
**
** WARNING! All changes made in this file will be lost!
****************************************************************************/

#include "form1.h"

#include <qvariant.h>
#include <qpushbutton.h>
#include <qgroupbox.h>
#include <qlabel.h>
#include <qlineedit.h>
#include <qtextedit.h>
#include <qlayout.h>
#include <qtooltip.h>
#include <qwhatsthis.h>
#include <qimage.h>
#include <qpixmap.h>

#include "form1.ui.h"
/*
 *  Constructs a Form1 as a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'.
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  TRUE to construct a modal dialog.
 */
Form1::Form1( QWidget* parent, const char* name, bool modal, WFlags fl )
    : QDialog( parent, name, modal, fl )
{
    if ( !name )
	setName( "Form1" );

    btnInit = new QPushButton( this, "btnInit" );
    btnInit->setGeometry( QRect( 490, 10, 100, 50 ) );

    groupBox1 = new QGroupBox( this, "groupBox1" );
    groupBox1->setGeometry( QRect( 11, 10, 460, 91 ) );

    textLabel1 = new QLabel( groupBox1, "textLabel1" );
    textLabel1->setGeometry( QRect( 20, 30, 56, 20 ) );

    txtWriteCount = new QLineEdit( groupBox1, "txtWriteCount" );
    txtWriteCount->setGeometry( QRect( 120, 30, 89, 22 ) );
    txtWriteCount->setAlignment( int( QLineEdit::AlignRight ) );

    textLabel1_3_3_2 = new QLabel( groupBox1, "textLabel1_3_3_2" );
    textLabel1_3_3_2->setGeometry( QRect( 140, 10, 60, 20 ) );

    textLabel1_3_3 = new QLabel( groupBox1, "textLabel1_3_3" );
    textLabel1_3_3->setGeometry( QRect( 235, 10, 75, 20 ) );

    txtWriteSpeed = new QLineEdit( groupBox1, "txtWriteSpeed" );
    txtWriteSpeed->setGeometry( QRect( 230, 30, 89, 22 ) );
    txtWriteSpeed->setAlignment( int( QLineEdit::AlignRight ) );

    txtReadSpeed = new QLineEdit( groupBox1, "txtReadSpeed" );
    txtReadSpeed->setGeometry( QRect( 230, 60, 89, 22 ) );
    txtReadSpeed->setAlignment( int( QLineEdit::AlignRight ) );

    txtWriteSectorCount = new QLineEdit( groupBox1, "txtWriteSectorCount" );
    txtWriteSectorCount->setGeometry( QRect( 355, 30, 89, 22 ) );
    txtWriteSectorCount->setAlignment( int( QLineEdit::AlignRight ) );

    txtReadSectorCount = new QLineEdit( groupBox1, "txtReadSectorCount" );
    txtReadSectorCount->setGeometry( QRect( 355, 60, 89, 22 ) );
    txtReadSectorCount->setAlignment( int( QLineEdit::AlignRight ) );

    textLabel1_3 = new QLabel( groupBox1, "textLabel1_3" );
    textLabel1_3->setGeometry( QRect( 350, 10, 100, 20 ) );

    textLabel1_2 = new QLabel( groupBox1, "textLabel1_2" );
    textLabel1_2->setGeometry( QRect( 20, 60, 56, 20 ) );

    txtReadCount = new QLineEdit( groupBox1, "txtReadCount" );
    txtReadCount->setGeometry( QRect( 120, 60, 89, 22 ) );
    txtReadCount->setAlignment( int( QLineEdit::AlignRight ) );

    groupBox2 = new QGroupBox( this, "groupBox2" );
    groupBox2->setGeometry( QRect( 10, 120, 460, 80 ) );

    textLabel1_2_2 = new QLabel( groupBox2, "textLabel1_2_2" );
    textLabel1_2_2->setGeometry( QRect( 10, 20, 99, 20 ) );

    txtGarbageCollectionNB = new QLineEdit( groupBox2, "txtGarbageCollectionNB" );
    txtGarbageCollectionNB->setGeometry( QRect( 120, 40, 89, 22 ) );
    txtGarbageCollectionNB->setAlignment( int( QLineEdit::AlignRight ) );

    txtGarbageCollection = new QLineEdit( groupBox2, "txtGarbageCollection" );
    txtGarbageCollection->setGeometry( QRect( 230, 40, 89, 22 ) );
    txtGarbageCollection->setAlignment( int( QLineEdit::AlignRight ) );

    txtGCStart = new QLineEdit( groupBox2, "txtGCStart" );
    txtGCStart->setGeometry( QRect( 355, 40, 89, 22 ) );
    txtGCStart->setAlignment( int( QLineEdit::AlignRight ) );

    textLabel1_4 = new QLabel( groupBox2, "textLabel1_4" );
    textLabel1_4->setGeometry( QRect( 140, 20, 66, 20 ) );

    textLabel2 = new QLabel( groupBox2, "textLabel2" );
    textLabel2->setGeometry( QRect( 238, 20, 75, 20 ) );

    textLabel3 = new QLabel( groupBox2, "textLabel3" );
    textLabel3->setGeometry( QRect( 380, 20, 77, 20 ) );

    groupBox3 = new QGroupBox( this, "groupBox3" );
    groupBox3->setGeometry( QRect( 10, 210, 270, 70 ) );

    textLabel1_2_2_3_2 = new QLabel( groupBox3, "textLabel1_2_2_3_2" );
    textLabel1_2_2_3_2->setGeometry( QRect( 10, 20, 70, 20 ) );

    textLabel4 = new QLabel( groupBox3, "textLabel4" );
    textLabel4->setGeometry( QRect( 70, 20, 56, 20 ) );

    txtTrimCount = new QLineEdit( groupBox3, "txtTrimCount" );
    txtTrimCount->setGeometry( QRect( 50, 40, 89, 22 ) );
    txtTrimCount->setAlignment( int( QLineEdit::AlignRight ) );

    txtTrimEffect = new QLineEdit( groupBox3, "txtTrimEffect" );
    txtTrimEffect->setGeometry( QRect( 160, 40, 89, 22 ) );
    txtTrimEffect->setAlignment( int( QLineEdit::AlignRight ) );

    textLabel5 = new QLabel( groupBox3, "textLabel5" );
    textLabel5->setGeometry( QRect( 182, 20, 56, 20 ) );

    textLabel1_2_2_3_2_2_2_2_2_2 = new QLabel( this, "textLabel1_2_2_3_2_2_2_2_2_2" );
    textLabel1_2_2_3_2_2_2_2_2_2->setGeometry( QRect( 250, 300, 150, 20 ) );

    txtWriteAmpCount = new QLineEdit( this, "txtWriteAmpCount" );
    txtWriteAmpCount->setGeometry( QRect( 370, 300, 89, 22 ) );
    txtWriteAmpCount->setAlignment( int( QLineEdit::AlignRight ) );

    txtSsdUtil = new QLineEdit( this, "txtSsdUtil" );
    txtSsdUtil->setGeometry( QRect( 370, 330, 89, 22 ) );
    txtSsdUtil->setAlignment( int( QLineEdit::AlignRight ) );

    textLabel1_2_3 = new QLabel( this, "textLabel1_2_3" );
    textLabel1_2_3->setGeometry( QRect( 250, 330, 56, 20 ) );

    textLabel1_2_2_3_2_2_2_2_2_2_2 = new QLabel( this, "textLabel1_2_2_3_2_2_2_2_2_2_2" );
    textLabel1_2_2_3_2_2_2_2_2_2_2->setGeometry( QRect( 30, 300, 100, 20 ) );

    txtWrittenBlockCount = new QLineEdit( this, "txtWrittenBlockCount" );
    txtWrittenBlockCount->setGeometry( QRect( 130, 300, 89, 22 ) );
    txtWrittenBlockCount->setAlignment( int( QLineEdit::AlignRight ) );

    textLabel1_2_2_3_2_2_2_2_2 = new QLabel( this, "textLabel1_2_2_3_2_2_2_2_2" );
    textLabel1_2_2_3_2_2_2_2_2->setGeometry( QRect( 280, 640, 110, 20 ) );

    txtWrittenBlockCount_2 = new QLineEdit( this, "txtWrittenBlockCount_2" );
    txtWrittenBlockCount_2->setGeometry( QRect( 470, 630, 89, 22 ) );
    txtWrittenBlockCount_2->setAlignment( int( QLineEdit::AlignRight ) );

    txtDebug = new QTextEdit( this, "txtDebug" );
    txtDebug->setGeometry( QRect( 10, 400, 460, 80 ) );
    txtDebug->setMouseTracking( FALSE );

    textLabel1_5 = new QLabel( this, "textLabel1_5" );
    textLabel1_5->setGeometry( QRect( 10, 370, 120, 20 ) );

    txtTimeProg = new QLineEdit( this, "txtTimeProg" );
    txtTimeProg->setGeometry( QRect( 130, 330, 89, 22 ) );
    txtTimeProg->setAlignment( int( QLineEdit::AlignRight ) );

    textLabel1_2_2_3_2_2_2_2_2_2_2_2 = new QLabel( this, "textLabel1_2_2_3_2_2_2_2_2_2_2_2" );
    textLabel1_2_2_3_2_2_2_2_2_2_2_2->setGeometry( QRect( 40, 330, 50, 20 ) );

    btnSave = new QPushButton( this, "btnSave" );
    btnSave->setGeometry( QRect( 490, 70, 100, 50 ) );
    languageChange();
    resize( QSize(600, 500).expandedTo(minimumSizeHint()) );
    clearWState( WState_Polished );

    // signals and slots connections
    connect( btnInit, SIGNAL( clicked() ), this, SLOT( btnInit_clicked() ) );
    connect( btnSave, SIGNAL( clicked() ), this, SLOT( btnSave_clicked() ) );
    init();
}

/*
 *  Destroys the object and frees any allocated resources
 */
Form1::~Form1()
{
    // no need to delete child widgets, Qt does it all for us
}

/*
 *  Sets the strings of the subwidgets using the current
 *  language.
 */
void Form1::languageChange()
{
    setCaption( tr( "SSD Monitor" ) );
    btnInit->setText( tr( "Initialize " ) );
    groupBox1->setTitle( QString::null );
    textLabel1->setText( tr( "Write:" ) );
    txtWriteCount->setText( tr( "0" ) );
    textLabel1_3_3_2->setText( tr( "Count" ) );
    textLabel1_3_3->setText( tr( "Speed [MB/s]" ) );
    txtWriteSpeed->setText( tr( "0" ) );
    txtReadSpeed->setText( tr( "0" ) );
    txtWriteSectorCount->setText( tr( "0" ) );
    txtReadSectorCount->setText( tr( "0" ) );
    textLabel1_3->setText( tr( "Sector Count" ) );
    textLabel1_2->setText( tr( "Read:" ) );
    txtReadCount->setText( tr( "0" ) );
    groupBox2->setTitle( tr( "Garbage Collection" ) );
    textLabel1_2_2->setText( QString::null );
    txtGarbageCollectionNB->setText( tr( "0" ) );
    txtGarbageCollection->setText( tr( "0" ) );
    txtGCStart->setText( tr( "0" ) );
    textLabel1_4->setText( tr( "Count" ) );
    textLabel2->setText( tr( "Exchange" ) );
    textLabel3->setText( tr( "Sector" ) );
    groupBox3->setTitle( tr( "TRIM" ) );
    textLabel1_2_2_3_2->setText( QString::null );
    textLabel4->setText( tr( "Count" ) );
    txtTrimCount->setText( tr( "0" ) );
    txtTrimEffect->setText( tr( "0" ) );
    textLabel5->setText( tr( "Effect" ) );
    textLabel1_2_2_3_2_2_2_2_2_2->setText( tr( "Write Amplification" ) );
    txtWriteAmpCount->setText( tr( "0" ) );
    txtSsdUtil->setText( tr( "0" ) );
    textLabel1_2_3->setText( tr( "SSD Util" ) );
    textLabel1_2_2_3_2_2_2_2_2_2_2->setText( tr( "Written Page" ) );
    txtWrittenBlockCount->setText( tr( "0" ) );
    textLabel1_2_2_3_2_2_2_2_2->setText( tr( "Written Block Count" ) );
    txtWrittenBlockCount_2->setText( tr( "0" ) );
    textLabel1_5->setText( tr( "Debug Status" ) );
    txtTimeProg->setText( tr( "0" ) );
    textLabel1_2_2_3_2_2_2_2_2_2_2_2->setText( tr( "Time" ) );
    btnSave->setText( tr( "Save" ) );
}

