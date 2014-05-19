/****************************************************************************
** Form1 meta object code from reading C++ file 'form1.h'
**
** Created: Thu Dec 19 06:50:48 2013
**      by: The Qt MOC ($Id: qt/moc_yacc.cpp   3.3.8   edited Feb 2 14:59 $)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#undef QT_NO_COMPAT
#include "form1.h"
#include <qmetaobject.h>
#include <qapplication.h>

#include <private/qucomextra_p.h>
#if !defined(Q_MOC_OUTPUT_REVISION) || (Q_MOC_OUTPUT_REVISION != 26)
#error "This file was generated using the moc from 3.3.8b. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

const char *Form1::className() const
{
    return "Form1";
}

QMetaObject *Form1::metaObj = 0;
static QMetaObjectCleanUp cleanUp_Form1( "Form1", &Form1::staticMetaObject );

#ifndef QT_NO_TRANSLATION
QString Form1::tr( const char *s, const char *c )
{
    if ( qApp )
	return qApp->translate( "Form1", s, c, QApplication::DefaultCodec );
    else
	return QString::fromLatin1( s );
}
#ifndef QT_NO_TRANSLATION_UTF8
QString Form1::trUtf8( const char *s, const char *c )
{
    if ( qApp )
	return qApp->translate( "Form1", s, c, QApplication::UnicodeUTF8 );
    else
	return QString::fromUtf8( s );
}
#endif // QT_NO_TRANSLATION_UTF8

#endif // QT_NO_TRANSLATION

QMetaObject* Form1::staticMetaObject()
{
    if ( metaObj )
	return metaObj;
    QMetaObject* parentObject = QDialog::staticMetaObject();
    static const QUMethod slot_0 = {"init_var", 0, 0 };
    static const QUMethod slot_1 = {"btnConnect_clicked", 0, 0 };
    static const QUMethod slot_2 = {"onReceive", 0, 0 };
    static const QUMethod slot_3 = {"btnInit_clicked", 0, 0 };
    static const QUMethod slot_4 = {"btnSave_clicked", 0, 0 };
    static const QUMethod slot_5 = {"languageChange", 0, 0 };
    static const QUMethod slot_6 = {"onTimer", 0, 0 };
    static const QMetaData slot_tbl[] = {
	{ "init_var()", &slot_0, QMetaData::Public },
	{ "btnConnect_clicked()", &slot_1, QMetaData::Public },
	{ "onReceive()", &slot_2, QMetaData::Public },
	{ "btnInit_clicked()", &slot_3, QMetaData::Public },
	{ "btnSave_clicked()", &slot_4, QMetaData::Public },
	{ "languageChange()", &slot_5, QMetaData::Protected },
	{ "onTimer()", &slot_6, QMetaData::Private }
    };
    metaObj = QMetaObject::new_metaobject(
	"Form1", parentObject,
	slot_tbl, 7,
	0, 0,
#ifndef QT_NO_PROPERTIES
	0, 0,
	0, 0,
#endif // QT_NO_PROPERTIES
	0, 0 );
    cleanUp_Form1.setMetaObject( metaObj );
    return metaObj;
}

void* Form1::qt_cast( const char* clname )
{
    if ( !qstrcmp( clname, "Form1" ) )
	return this;
    return QDialog::qt_cast( clname );
}

bool Form1::qt_invoke( int _id, QUObject* _o )
{
    switch ( _id - staticMetaObject()->slotOffset() ) {
    case 0: init_var(); break;
    case 1: btnConnect_clicked(); break;
    case 2: onReceive(); break;
    case 3: btnInit_clicked(); break;
    case 4: btnSave_clicked(); break;
    case 5: languageChange(); break;
    case 6: onTimer(); break;
    default:
	return QDialog::qt_invoke( _id, _o );
    }
    return TRUE;
}

bool Form1::qt_emit( int _id, QUObject* _o )
{
    return QDialog::qt_emit(_id,_o);
}
#ifndef QT_NO_PROPERTIES

bool Form1::qt_property( int id, int f, QVariant* v)
{
    return QDialog::qt_property( id, f, v);
}

bool Form1::qt_static_property( QObject* , int , int , QVariant* ){ return FALSE; }
#endif // QT_NO_PROPERTIES
