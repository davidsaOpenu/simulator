import argparse
from pathlib import Path
from time import sleep

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
    parser.add_argument(
        "-t",
        "--timeout",
        type=int,
        default=60,
        help="Amount of time to wait for elasticsearch to start. -1 for infinte, 0 for none",
    )

    return parser.parse_args()


def run_tests(
    client: elasticsearch.Elasticsearch, index_template: str, expected_logs_count: int
) -> None:
    search = Search(using=client, index=index_template)
    es_docs_count = search.count()

    assert (
        es_docs_count == expected_logs_count
    ), f"{es_docs_count} documents in elasticsearch, {expected_logs_count} log lines expected from files."


def count_log_lines_in_dir(dir: Path) -> int:
    log_file_lens = {}
    for log_file_path in dir.iterdir():
        with log_file_path.open("r") as f:
            log_file_lens[log_file_path.absolute().as_posix()] = len(
                list(filter(bool, f.readlines()))
            )

    return sum(log_file_lens.values())


def wait_for_elasticsearch(client: elasticsearch.Elasticsearch, timeout: int) -> None:
    attempts = 0
    while (attempts := attempts + 1) <= timeout or timeout == -1:
        try:
            cluster_health = client.cluster.health()
            return
        except elasticsearch.exceptions.ConnectionError as e:
            sleep(1)

    raise TimeoutError(f"Elasticsearch not up in {timeout} seconds.")


def main():
    args = parse_args()

    if args.elasticsearch_username:
        es_url = f"http://{args.elasticsearch_username}:{args.elasticsearch_password}@{args.elasticsearch_host}:{args.elasticsearch_port}/"
    else:
        es_url = f"http://{args.elasticsearch_host}:{args.elasticsearch_port}/"

    elasticsearch_client = elasticsearch.Elasticsearch(hosts=[es_url])

    if args.timeout != 0:
        wait_for_elasticsearch(elasticsearch_client, args.timeout)

    sleep(60)  # Elastic is now up, give filebeat time to index

    run_tests(
        elasticsearch_client,
        args.elasticsearch_index_template,
        count_log_lines_in_dir(args.logs_directory),
    )


if __name__ == "__main__":
    main()
