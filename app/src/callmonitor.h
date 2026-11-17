#ifndef CALLMONITOR_H
#define CALLMONITOR_H

#include <QObject>
#include <QSet>
#include <QString>

class QTimer;

class CallMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool ringing READ ringing NOTIFY ringingChanged)
    Q_PROPERTY(QString caller READ caller NOTIFY ringingChanged)

public:
    explicit CallMonitor(QObject *parent = 0);

    QString status() const { return m_status; }
    bool ringing() const { return m_ringing; }
    QString caller() const { return m_caller; }

signals:
    void statusChanged();
    void ringingChanged();
    void callAlert(bool ringing, const QString &caller);

public slots:
    void setIncoming(bool incoming, const QString &caller);

private slots:
    void pulse();
    void flushStop();
    void refreshCalls();

private:
    void setStatus(const QString &text);
    void applyRinging(bool ringing, const QString &caller);
    QStringList voiceModems();
    void subscribeModem(const QString &path);
    void subscribeCall(const QString &path);

    QTimer *m_repeat;
    QTimer *m_ofonoTimer;
    QString m_status;
    QString m_caller;
    QSet<QString> m_modems;
    QSet<QString> m_watchedCalls;
    bool m_ringing;
    bool m_ofonoReady;
    bool m_refreshing;
    bool m_ofonoErrorLogged;
    int m_stopBursts;
};

#endif
