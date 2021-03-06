#include <QStringList>
#include <QString>
#include <QSet>
#include <QDebug>
#include <QRegExp>
#include "sipparser.h"
#include "sipuri.h"
#include "sipmessage.h"
#include "sipdefinitions.h"
#include "headerlineparser_to.h"
#include "headerlineparser_call_id.h"
#include "headerlineparser_from.h"
#include "headerlineparser_via.h"
#include "headerlineparser_cseq.h"
#include "headerlineparser_max_forwards.h"
#include "headerlineparser_contact.h"
#include "headerlineparser_expires.h"
#include "headerlineparser_content_length.h"

SipParser::SipParser(QObject *parent) : QObject(parent)
{

}

// split by a delimitter character, except when character between unescaped double quotes
void SipParser::splitBy(QString inputline, QChar delimitter, QStringList* outputlist)
{
  bool inside_quotes = false;
  bool prev_is_backslash = false;
  int start_of_strpart = 0;

  for (int i=0; i < inputline.size(); i++)
  {
    QString curr_char = inputline.mid(i, 1);

    // Process current character
    if (inside_quotes)
    {
      // check for closing quote
      if (curr_char == "\"")
      {
        if (!prev_is_backslash)
        {
          inside_quotes = false;
        }
      }
      else
      {
        // Set variables for next iteration
        if (curr_char == "\\")
        {
          if (prev_is_backslash)
          {
            prev_is_backslash = false;
          }
          else
          {
            prev_is_backslash = true;
          }
        }
        else
        {
          prev_is_backslash = false;
        }
      }
    }
    else
    {
      if (curr_char == QString(delimitter))
      {
        outputlist->append(inputline.mid(start_of_strpart, i-start_of_strpart));
        start_of_strpart = i+1;
      }
      if (curr_char == "\"")
      {
        // found opening double quote
        inside_quotes = true;
        prev_is_backslash = false;
      }
    }
    // qDebug() << inputline.left(i+1) << inside_quotes << prev_is_backslash;
  }
  // add leftover
  outputlist->append(inputline.mid(start_of_strpart));
}

// find the first position of a delimitter character, except when character between unescaped double quotes
int SipParser::findFirstLiteral(QString inputline, QChar delimitter)
{
  bool inside_quotes = false;
  bool prev_is_backslash = false;

  for (int i=0; i < inputline.size(); i++)
  {
    QString curr_char = inputline.mid(i, 1);

    // Process current character
    if (inside_quotes)
    {
      // check for closing quote
      if (curr_char == "\"")
      {
        if (!prev_is_backslash)
        {
          inside_quotes = false;
        }
      }
      else
      {
        // Set variables for next iteration
        if (curr_char == "\\")
        {
          if (prev_is_backslash)
          {
            prev_is_backslash = false;
          }
          else
          {
            prev_is_backslash = true;
          }
        }
        else
        {
          prev_is_backslash = false;
        }
      }
    }
    else
    {
      if (curr_char == QString(delimitter))
      {
        return i;
      }
      if (curr_char == "\"")
      {
        // found opening double quote
        inside_quotes = true;
        prev_is_backslash = false;
      }
    }

  }

  return -1;
}

int SipParser::findLWS(QString strToSearch)
{
  for (int i=0; i < strToSearch.size(); i++)
  {
    QString curr_char = strToSearch.mid(i, 1);

    if ((curr_char == " ") || (curr_char == "\t"))
    {
      return i;
    }
  }
  return -1;
}


int SipParser::parseSipURI(QString uritext, SipURI* sipuri)
{
  // use regex
  QRegExp rx("^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?");
  if (rx.indexIn(uritext) == -1)
  {
    return 1;
  }
  QStringList list = rx.capturedTexts();
  if (list.size() < 10)
  {
    return 2;
  }
  sipuri->setUriText(uritext);
  if (list[2] == "")
  {
    return 3;
  }
  else
  {
    sipuri->setUriScheme(list[2]);
  }
  QString server;

  if (list[3].startsWith("//"))
  {
    // absolute url
    server = list[4];
    sipuri->setUriPath(list[5]);
  }
  else
  {
    // opaque: list[5] contains host and possibly path
    int pathpos = list[5].indexOf('/');
    if (pathpos < 0)
    {
      server = list[5];
    }
    else
    {
      if (pathpos < 1)
      {
        // no host
        return 4;
      }
      server = list[5].left(pathpos);
      sipuri->setUriPath(list[5].mid(pathpos));
    }
  }
  QString hostport;

  // split on ; to find parameters
  QStringList serverparts = server.split(";");
  server = serverparts[0];

  // parameters
  for (int i = 0; i < serverparts.size(); ++i)
  {
    int equalpos = serverparts[0].indexOf('=');
    QString paramname;
    QString paramvalue = "";
    if (equalpos > 0)
    {
      paramname = serverparts[0].left(equalpos).trimmed().toLower();
      paramvalue = serverparts[0].mid(equalpos+1).trimmed();
    }
    else
    {
      paramname = serverparts[0].trimmed().toLower();;
    }
    sipuri->addParams(paramname, paramvalue);
  }

  // hostport
  int atpos = server.indexOf('@');
  if (atpos < 0)
  {
    // no user info in server
    hostport = server;
  }
  else
  {
    sipuri->setUriUserinfo(server.left(atpos));
    hostport = server.mid(atpos+1);
  }
  int portpos = hostport.indexOf(':');
  if (portpos < 0)
  {
    sipuri->setUriHost(hostport);
  }
  else
  {
    sipuri->setUriHost(hostport.left(portpos));
    sipuri->setUriPort(hostport.mid(portpos+1));
  }
  sipuri->setUriQuery(list[7]);
  sipuri->setUriFragment(list[9]);

  return 0;
}

QString SipParser::getCallId(QByteArray sipheader)
{
  QString message = QString::fromUtf8(sipheader.data());
  QStringList messagelines = message.split("\n");
  for (int i = 0; i < messagelines.size(); ++i)
  {
    // TODO call id on multiple lines?
    if (messagelines[i].startsWith("Call-ID", Qt::CaseInsensitive) ||
        messagelines[i].startsWith("i ", Qt::CaseInsensitive) ||
        messagelines[i].startsWith("i\t", Qt::CaseInsensitive) ||
        messagelines[i].startsWith("i:", Qt::CaseInsensitive))
    {
      QStringList lineparts = messagelines[i].split(":");
      if (lineparts.size() > 1)
      {
        return lineparts[1].trimmed();
      }
    }
  }
  return NULL;
}

int SipParser::parse(QByteArray sipdata, SipMessage* sipmessage)
{
  QString message = QString::fromUtf8(sipdata.data());
  QStringList messagelines = message.split("\n");

  // discard messages that do not at least contain a startline and a CRLF line between header and body
  if (messagelines.size() < 2)
  {
    return 1;
  }

  QString startline = messagelines[0];

  // responses start with SIP
  if (startline.startsWith("SIP", Qt::CaseInsensitive))
  {
    // Response message
    sipmessage->setIsRequest(false);

    // Parse response startline, must be: SIP-Version SP Status-Code SP Reason-Phrase

    int firstspacepos = startline.indexOf(" ");
    if (firstspacepos < 1)
    {
      return 2;
    }
    sipmessage->setSipVersion(startline.left(firstspacepos));
    int secondspacepos = startline.indexOf(" ", firstspacepos+1);
    if (secondspacepos < firstspacepos+2)
    {
      return 3;
    }
    sipmessage->setStatusCode(startline.mid(firstspacepos+1, secondspacepos-(firstspacepos+1)));

    QString reason = startline.mid(secondspacepos+1).trimmed();
    if (reason.isNull() || reason.isEmpty())
    {
      return 4;
    }
    sipmessage->setReasonPhrase(reason);
  }
  else
  {
    // Request message
    sipmessage->setIsRequest(true);

    // Parse response startline, must be: Method SP Request-URI SP SIP-Version
    QStringList startlineparts = startline.split(" ");

    // all parts available?
    if (startlineparts.size() < 3)
    {
      return 5;
    }

    // check if sip method can be recognized
    if (!SipDefinitions::sipMethods.contains(startlineparts[0]))
    {
      qDebug() << "method not recognized";
      return 6;
    }

    sipmessage->setSipMethod(startlineparts[0]);

    // Parse sip URI
    qDebug() << "parse uri";
    SipURI* sipuri = new SipURI(sipmessage);
    int result = parseSipURI(startlineparts[1], sipuri);
    if (result != 0)
    {
      return 7;
    }
    sipmessage->setSipURI(sipuri);

    sipmessage->setSipVersion(startlineparts[2]);

  }

  // get all header lines and concat the lines that were on multiple lines
  int i = 1;
  bool header_end = false;
  QStringList headerlines;
  while (i < messagelines.size() && !header_end)
  {
    if (messagelines[i].size() == 0 || messagelines[i] == "\r")
    {
      header_end = true;
    }
    else
    {
      // is this a contination line?
      if (messagelines[i].left(1) == " " || messagelines[i].left(1) == "\t")
      {
        // add to previous header line
        if (headerlines.count() > 0)
        {
          headerlines[headerlines.count()-1].append(" ").append(messagelines[i].trimmed());
        }
        else
        {
          // first line can not be contination line
        }
      }
      else
      {
        // new header line
        headerlines << messagelines[i];
      }
    }
    i++;
  }

  if (parseHeader(headerlines, sipmessage) < 0)
  {
    return 8;
  }

  if (i < messagelines.size())
  {
    // get body: find empty line
    int emptylinepos = message.indexOf("\r\n\r\n");
    int bodypos;
    if (emptylinepos < 0)
    {
      int emptylinepos = message.indexOf("\n\n");
      if (emptylinepos < 0)
      {
        return 9;
      }
      else
      {
        bodypos = emptylinepos+2;
      }
    }
    else
    {
      bodypos = emptylinepos+4;
    }
    sipmessage->setSipBody(message.mid(bodypos));
  }


  return 0;
}

int SipParser::parseHeader(QStringList sipheader, SipMessage* sipmessage)
{
  for (int i = 0; i < sipheader.size(); i++)
  {

    int colonpos = sipheader[i].indexOf(':');
    if (colonpos > 0)
    {
      QString fieldname = sipheader[i].left(colonpos).trimmed();
      QString fieldvalues = sipheader[i].mid(colonpos+1).trimmed();
      qDebug() << fieldname << "fv:" << fieldvalues;
      getHeaderlineparser(fieldname)->parse(fieldvalues, sipmessage);
    }
  }
  return 0;
}

int SipParser::parseSipAdress(QString text_to_parse, SipAdress* sip_address)
{
  sip_address->setText(text_to_parse);
  int laquotpos = SipParser::findFirstLiteral(text_to_parse, '<');
  int raquotpos = SipParser::findFirstLiteral(text_to_parse, '>');
  qDebug() << laquotpos << raquotpos;
  QString addrspec;
  int semipos;

  if ((laquotpos >= 0)  && (raquotpos > 0) && (laquotpos < raquotpos))
  {
    // name-addr form, see rfc
    sip_address->setDisplayname(text_to_parse.left(laquotpos));
    addrspec = text_to_parse.mid(laquotpos+1, raquotpos-(laquotpos+1));
    semipos = SipParser::findFirstLiteral(text_to_parse.mid(raquotpos+1), ';');

  }
  else
  {
    // addr-spec form, see rfc
    sip_address ->setDisplayname("");
    semipos = SipParser::findFirstLiteral(text_to_parse, ';');
    if (semipos < 0)
    {
      addrspec = text_to_parse;
    }
    else
    {
      addrspec = text_to_parse.left(semipos);
    }
  }
  if (parseSipURI(addrspec, sip_address->getUri()) != 0)
  {
    qDebug() << "4";
    return 4;
  }
      qDebug() << "as: "<< addrspec;
  while (semipos > 0)
  {
    int start = semipos+1;

    // find next semicolon
    semipos = SipParser::findFirstLiteral(text_to_parse.mid(start), ';');
    int end;
    if (semipos > 0)
    {
      end = semipos;
    }
    else
    {
      end = text_to_parse.size();
    }
    QString paramstr = text_to_parse.mid(start, end-start);
    int equalpos = paramstr.indexOf('=');
    QString paramname;
    QString paramvalue = "";
    if (equalpos > 0)
    {
      paramname = paramstr.left(equalpos).trimmed().toLower();
      paramvalue = paramstr.mid(equalpos+1).trimmed();
    }
    else
    {
      paramname = paramstr.trimmed().toLower();;
    }
    sip_address->addParams(paramname, paramvalue);
  }
  return 0;
}

void SipParser::initHeaderlineparsers()
{
  headerlineparsers.insert("call-id", new HeaderLineParser_Call_Id());
  headerlineparsers.insert("i", new HeaderLineParser_Call_Id());
  headerlineparsers.insert("contact", new HeaderLineParser_Contact());
  headerlineparsers.insert("m", new HeaderLineParser_Contact());
  headerlineparsers.insert("content-length", new HeaderLineParser_Content_Length());
  headerlineparsers.insert("l", new HeaderLineParser_Content_Length());
  headerlineparsers.insert("cseq", new HeaderLineParser_Cseq());
  headerlineparsers.insert("expires", new HeaderLineParser_Expires());
  headerlineparsers.insert("from", new HeaderLineParser_From());
  headerlineparsers.insert("f", new HeaderLineParser_From());
  headerlineparsers.insert("max-forwards", new HeaderLineParser_Max_Forwards());
  headerlineparsers.insert("to", new HeaderLineParser_To());
  headerlineparsers.insert("t", new HeaderLineParser_To());
  headerlineparsers.insert("via", new HeaderLineParser_Via());
  headerlineparsers.insert("v", new HeaderLineParser_Via());
}

HeaderLineParser* SipParser::getHeaderlineparser(QString field)
{
  if (headerlineparsers.isEmpty())
  {
    initHeaderlineparsers();
  }
  HeaderLineParser* parser = headerlineparsers.value(field.toLower());
  if (parser != NULL)
  {
    qDebug() << "parser not null";
    return parser;
  }
  else
  {
    qDebug() << "parser null";
    // TODO parser for non-defined fields
    return new HeaderLineParser();
  }
}

QHash<QString, HeaderLineParser*> SipParser::headerlineparsers;
