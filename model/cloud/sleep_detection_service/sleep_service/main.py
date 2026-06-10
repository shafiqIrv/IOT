from __future__ import annotations

from .config import load_settings
from .service import run_service


def main() -> None:
  run_service(load_settings())


if __name__ == "__main__":
  main()
