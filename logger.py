# logger.py
import logging
from logging.handlers import RotatingFileHandler
import os

LOG_DIR = "/Users/krzysztofbienias/Documents/logs"
os.makedirs(LOG_DIR, exist_ok=True)

LOG_FILE = os.path.join(LOG_DIR, "numeraire.log") #TODO move this to config json

# Formatter
formatter = logging.Formatter("[%(asctime)s] %(levelname)s in %(name)s: %(message)s")

# File Handler with rotation
file_handler = RotatingFileHandler(LOG_FILE, maxBytes=1_000_000, backupCount=5)
file_handler.setFormatter(formatter)
file_handler.setLevel(logging.DEBUG)

# Console Handler (optional)
console_handler = logging.StreamHandler()
console_handler.setFormatter(formatter)
console_handler.setLevel(logging.INFO)

# Logger setup
logger = logging.getLogger("numeraire_logger")
logger.setLevel(logging.DEBUG)
logger.addHandler(file_handler)
logger.addHandler(console_handler)
logger.propagate = False