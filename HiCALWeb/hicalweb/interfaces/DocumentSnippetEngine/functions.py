from config.settings.base import DOCUMENTS_URL
from config.settings.base import PARA_URL

from config.settings.base import CAL_SERVER_IP
from config.settings.base import CAL_SERVER_PORT

import httplib2
import requests
import urllib.parse

from bs4 import BeautifulSoup
from bs4.element import Comment

try:
    # For c speedups
    from simplejson import loads
except ImportError:
    from json import loads


def get_date(content):
    for line in content.split('\n'):
        if line.strip()[:4] == "Sent":
            return line.split(':', 1)[1].strip()
    return ""


def get_subject(content):
    for line in content.split('\n'):
        if line.strip()[:7] == "Subject":
            return line.split(':', 1)[1].strip()
    return ""

#blacklist = ['style', 'script', 'head', 'title', 'meta', '[document]']
blacklist = ['style', 'script']

def get_text_from_html(body):
    soup = BeautifulSoup(body, features="html.parser")
    soup = soup.find('html')
    if not soup:
        return "No text in the HTML\n"
    # kill all script and style elements
    for script in soup(blacklist):
        script.extract()    # rip it out
    # get text
    text = soup.get_text()

    # break into lines and remove leading and trailing space on each
    lines = (line.strip() for line in text.splitlines())
    # break multi-headlines into a line each
    chunks = (phrase.strip() for line in lines for phrase in line.split("  "))
    # drop blank lines
    text = '\n'.join(chunk for chunk in chunks if chunk)
    return text

def get_documents(doc_ids, query=None):
    """
    :param query:
    :param doc_ids: the ids of documents to return
    :return: documents content
    """
    result = []
    h = httplib2.Http()
    for doc_id in doc_ids:
        parameters = {'doc_id': doc_id}
        #with open(doc_id, "r") as doc:

        parameters = urllib.parse.urlencode(parameters)
        url = "http://{}:{}/CAL/get_content?".format(CAL_SERVER_IP, CAL_SERVER_PORT)
        #resp, content = h.request(url + parameters, method="GET")
        r = requests.get(url + parameters)
        doc_content, date, title = "", "", ""
        if r and r.status_code == 200:
            #content = loads(r.content.decode("iso-8859-1"), strict=False)
            doc_content = r.content.decode("iso-8859-1")
            text_content = get_text_from_html(doc_content)
        if len(doc_content) == 0:
            if len(title) == 0:
                title = '<i class="text-warning">The document title is empty</i>'
            doc_content = '<i class="text-warning">The document content is empty</i>'
        else:
            if len(title) == 0:
                title = text_content[:32]

        document = {
            'doc_id': doc_id,
            'title': title,
            'content': text_content.replace('\n', '<br>')
        }
        result.append(document)

    return result


def get_documents_with_snippet(doc_ids, query):
    h = httplib2.Http()
    url = "{}/{}"
    doc_ids_unique = []
    doc_ids_set = set()
    for doc_id in doc_ids:
        if doc_id['doc_id'] not in doc_ids_set:
            doc_ids_set.add(doc_id['doc_id'])
            doc_ids_unique.append(doc_id)

    doc_ids = doc_ids_unique

    result = get_documents([doc['doc_id'] for doc in doc_ids], query)
    for doc_para_id, doc in zip(doc_ids, result):
        if 'para_id' not in doc_para_id:
            doc['snippet'] = u'N/A'
            continue
        try:
            para_id = doc_para_id['doc_id'] + '.' + doc_para_id['para_id']
            resp, content = h.request(url.format(PARA_URL, para_id),
                                      method="GET")
            doc['snippet'] = content.decode('utf-8', 'ignore').replace("\n", "<br />");
        except:
            doc['snippet'] = u'N/A'
    return result
