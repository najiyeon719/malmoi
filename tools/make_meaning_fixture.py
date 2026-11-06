"""뜻 기준 유사도 테스트용 사전: 정답을 미리 아는 단어 7개(뜻 9개)."""
import argparse
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare import build, write_db

DIALECT = "‘베’의 방언"
# (단어, 뜻풀이, 벡터). 같은 단어를 여러 줄 쓰면 그 단어의 뜻이 여러 개가 돼요.
SENSES = [
    ("비", "하늘에서 내리는 물방울.", [1.0, 0.0, 0.0]),
    ("비", DIALECT, [0.0, 1.0, 0.0]),
    ("비-", "‘힘껏’의 뜻을 더하는 접두사.", [0.0, 0.0, 1.0]),
    ("감", DIALECT, [0.0, 1.0, 0.0]),
    ("감", "가을에 익는 주황색 열매.", [0.6, 0.8, 0.0]),
    ("이슬비", "아주 가늘게 내리는 비.", [0.9, 0.0, 0.43588989]),
    ("힘껏", "있는 힘을 다하여.", [0.0, 0.0, 1.0]),
    ("벼", DIALECT, [0.0, 1.0, 0.0]),
    ("불", DIALECT, [0.0, 1.0, 0.0]),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/fixtures/meaning.mlm"))
    args = parser.parse_args()
    rows = []
    for i, (word, definition, vector) in enumerate(SENSES):
        rows.append({"id": f"s{i:02d}", "lexeme_id": f"w{i:02d}", "word": word,
                     "definition": definition, "word_type": "고유어", "pos": "명사",
                     "word_vector": vector})
    space = {"model_id": "meaning-test", "revision": "1", "dimension": 3, "dtype": "float32",
             "pooling": "none", "normalization": "none",
             "query_prefix": "", "document_prefix": "", "synthetic": True}
    source = "|".join(f"{w}:{d}" for w, d, _ in SENSES).encode()
    db = build(rows, {"word": space, "sense": None}, dataset_version="meaning-9-v1",
               source_sha256=hashlib.sha256(source).hexdigest())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_db(args.output, db)
    print(f"{len(rows)} senses -> {args.output}")


if __name__ == "__main__":
    main()