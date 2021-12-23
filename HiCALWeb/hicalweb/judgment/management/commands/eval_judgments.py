from django.core.management.base import BaseCommand

from hicalweb.judgment.models import Judgment
from hicalweb.progress.models import Session
from config.settings.base import CAL_SERVER_IP, CAL_SERVER_PORT
import time
import requests


class Command(BaseCommand):
    help = 'Evaluate judgments for a session based on TREC qrels'

    def add_arguments(self, parser):
        parser.add_argument('session_id', type=str)

    def get_idx_and_offset(self, doc_id):
        parts = doc_id.split('/')
        offset = parts[-1]
        idx_file = "/".join(parts[:-1])[:-4]
        return idx_file, offset
        
    def handle(self, *args, **option):
        #self.stdout.write(self.style.SUCCESS("Starting"))
        session_id = option['session_id']
        #self.stdout.write(session_id)
        
        sesh = Session.objects.filter(uuid=session_id)
        judgments = Judgment.objects.filter(task = sesh)
        outfile = "labels/" + session_id + ".labels.txt"
        with open(outfile, "w") as out:
            for judgment in judgments:
                doc_id, rel, time = judgment.doc_id, judgment.relevance, judgment.created_at
                idx_file, offset = self.get_idx_and_offset(doc_id)
                time_str = str(time)
                time_str = ":".join(time_str.split())
                out.write(idx_file + " " + offset + " " + str(rel) + " " + time_str + "\n")

        #idx_file, offset = self.get_idx_and_offset("/mnt/a536sing/sdc1/Disk1/0000wb/0000wb-90.warc.gz.idx.dir/187320756")
        #self.stdout.write(idx_file + " " + offset)

        #self.stdout.write(self.style.SUCCESS(
        #    'Requests for all sessions are completed.'))
