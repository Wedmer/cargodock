#include "davapi.h"
#include "../network.h"

#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QIODevice>
#include <QList>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

#include <QDebug>

#include <locale.h>
#include <time.h>

namespace
{

const QString NS_DAV("DAV:");

struct Resource
{
    QString name;
    QString href;
    QString resourceType;
    QString contentType;
    int contentLength;
    QDateTime lastModified;

};

QDateTime fromTimeString(const QString& s)
{
    if (s.isEmpty())
    {
        return QDateTime();
    }

    struct tm tm;
    setlocale(LC_TIME, "C");
    strptime(s.toLatin1().constData(), "%a, %d %b %Y %H:%M:%S %z" , &tm);
    setlocale(LC_TIME, "");
    //qDebug() << "time" << s << "->" << QDateTime::fromTime_t(timegm(&tm));
    return QDateTime::fromTime_t(timegm(&tm));
}

DavApi::Properties parseResult(const QDomElement& elem)
{
    DavApi::Properties props;

    QDomNodeList nodes = elem.elementsByTagNameNS(NS_DAV, "href");
    if (nodes.length())
    {
        const QString href = nodes.at(0).toElement().text();
        const QStringList parts = href.split("/");
        props.name = QUrl::fromPercentEncoding(
                    (parts.size() ? parts.last() : QString()).toUtf8());
        props.href = href;
    }

    nodes = elem.elementsByTagNameNS(NS_DAV, "resourcetype");
    if (nodes.length())
    {
        const QDomNode typeNode = nodes.at(0).firstChild();
        props.resourceType = typeNode.toElement().tagName();
    }

    nodes = elem.elementsByTagNameNS(NS_DAV, "getcontenttype");
    if (nodes.length())
    {
        props.contentType = nodes.at(0).toElement().text();
    }

    nodes = elem.elementsByTagNameNS(NS_DAV, "getlastmodified");
    if (nodes.length())
    {
        props.lastModified = fromTimeString(nodes.at(0).toElement().text());
    }

    nodes = elem.elementsByTagNameNS(NS_DAV, "getcontentlength");
    if (nodes.length())
    {
        props.contentLength = nodes.at(0).toElement().text().toInt();
    }

    return props;
}

QList<DavApi::Properties> parseResult(const QByteArray& data)
{
    QList<DavApi::Properties> result;

    QDomDocument doc;
    doc.setContent(data, true);

    QDomNodeList nodes = doc.elementsByTagNameNS(NS_DAV, "response");
    for (int i = 0; i < nodes.length(); ++i)
    {
        QDomNode node = nodes.at(i);
        if (node.isElement())
        {
            result << parseResult(node.toElement());
        }
    }

    return result;
}

}


DavApi::DavApi(QObject* parent)
    : QObject(parent)
{
}

void DavApi::propfind(const QString& path)
{
    QNetworkAccessManager* nam = Network::accessManager();
    if (nam)
    {
        qDebug() << Q_FUNC_INFO << path;
        QUrl url(myAddress);
        url.setPath(QUrl::toPercentEncoding(path, "/"));

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentLengthHeader, QVariant(0));
        req.setRawHeader("Depth", "1");
        QNetworkReply* reply = nam->sendCustomRequest(req, "PROPFIND");

        connect(reply, SIGNAL(finished()),
                this, SLOT(slotPropfindReceived()));
    }
}

void DavApi::mkcol(const QString& path)
{
    QNetworkAccessManager* nam = Network::accessManager();
    if (nam)
    {
        QUrl url(myAddress);
        url.setPath(QUrl::toPercentEncoding(path, "/"));

        QNetworkRequest req(url);
        QNetworkReply* reply = nam->sendCustomRequest(req, "MKCOL");

        connect(reply, SIGNAL(finished()),
                this, SLOT(slotMkColFinished()));
    }
}

void DavApi::deleteResource(const QString& path)
{
    QNetworkAccessManager* nam = Network::accessManager();
    if (nam)
    {
        QUrl url(myAddress);
        url.setPath(QUrl::toPercentEncoding(path, "/"));

        QNetworkRequest req(url);
        QNetworkReply* reply = nam->deleteResource(req);

        connect(reply, SIGNAL(finished()),
                this, SLOT(slotDeleteFinished()));
    }
}

void DavApi::moveResource(const QString& path, const QString& newPath)
{
    QNetworkAccessManager* nam = Network::accessManager();
    if (nam)
    {
        QUrl url(myAddress);
        url.setPath(QUrl::toPercentEncoding(path, "/"));

        QNetworkRequest req(url);
        req.setRawHeader("Destination", QUrl::toPercentEncoding(newPath, "/"));
        QNetworkReply* reply = nam->sendCustomRequest(req, "MOVE");

        connect(reply, SIGNAL(finished()),
                this, SLOT(slotMoveFinished()));
    }
}

QNetworkReply* DavApi::getResource(const QString& path, qint64 offset, qint64 size)
{
    qDebug() << Q_FUNC_INFO << path << offset << size;
    QNetworkReply* reply = 0;

    QNetworkAccessManager* nam = Network::accessManager();
    if (nam)
    {
        QUrl url(myAddress);
        url.setPath(QUrl::toPercentEncoding(path, "/"));

        QNetworkRequest req(url);
        reply = nam->get(req);
        reply->setProperty("identifier", path);


        connect(reply, SIGNAL(finished()),
                this, SLOT(slotResourceReceived()));
    }

    return reply;
}

void DavApi::putResource(const QString& path, QIODevice* buffer)
{
    QNetworkAccessManager* nam = Network::accessManager();
    if (nam)
    {
        QUrl url(myAddress);
        url.setPath(QUrl::toPercentEncoding(path, "/"));

        QNetworkRequest req(url);
        QNetworkReply* reply = nam->put(req, buffer);
        reply->setProperty("identifier", path);

        connect(reply, SIGNAL(finished()),
                this, SLOT(slotPutFinished()));
    }
}

void DavApi::slotPropfindReceived()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    int result = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    qDebug() << Q_FUNC_INFO << result << data;

    QList<Properties> properties = parseResult(data);
    foreach (const Properties& props, properties.mid(1))
    {
        qDebug() << props.href;
        emit propertiesReceived(props);
    }
    Properties nullProps;
    emit propertiesReceived(nullProps);
}

void DavApi::slotMkColFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    int result = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "MKCOL finished" << result;
    emit mkColFinished(result);

    reply->deleteLater();
}

void DavApi::slotDeleteFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    int result = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "DELETE finished" << result;
    emit deleteFinished(result);

    reply->deleteLater();
}

void DavApi::slotMoveFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    int result = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "MOVE finished" << result;
    emit moveFinished(result);

    reply->deleteLater();
}

void DavApi::slotResourceReceived()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    int result = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString path = reply->property("identifier").toString();
    qDebug() << "GET finished" << result;
    emit resourceReceived(path, result);

    // do not delete the reply object here; it was handed outside
}

void DavApi::slotPutFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    int result = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString path = reply->property("identifier").toString();
    qDebug() << "PUT finished" << result;
    emit putFinished(path, result);

    reply->deleteLater();
}
