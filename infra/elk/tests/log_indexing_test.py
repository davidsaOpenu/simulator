import argparse
from pathlib import Path

import elasticsearch
from elasticsearch_dsl import Search


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-l", "--logs-directory", type=Path, required=True)
    parser.add_argument("-H", "--elasticsearch-host", type=str, required=True)
    parser.add_argument("-p", "--elasticsearch-port", type=int, required=True)
    parser.add_argument("-i", "--elasticsearch-index-template", type=str, required=True)
    parser.add_argument("-u", "--elasticsearch-username", type=str, required=False)
    parser.add_argument("-P", "--elasticsearch-password", type=str, required=False)

    return parser.parse_args()


def main():
    args = parse_args()

    if args.elasticsearch_username:
        es_url = f"http://{args.elasticsearch_username}:{args.elasticsearch_password}@{args.elasticsearch_host}:{args.elasticsearch_port}/"
    else:
        es_url = f"http://{args.elasticsearch_host}:{args.elasticsearch_port}/"

    elasticsearch_client = elasticsearch.Elasticsearch(hosts=[es_url])
    search = Search(using=elasticsearch_client, index=args.elasticsearch_index_template)
    response = search.execute()

    log_file_lens = {}
    for log_file_path in args.logs_directory.iterdir():
        with log_file_path.open("r") as f:
            log_file_lens[log_file_path.absolute().as_posix()] = len(
                list(filter(bool, f.readlines()))
            )

    es_docs_count = search.count()
    local_logs_count = sum(log_file_lens.values())

    assert es_docs_count == sum(
        log_file_lens.values()
    ), f"{es_docs_count} documents in elasticsearch, {local_logs_count} log lines in files."


if __name__ == "__main__":
    main()
