import json
import os

CONFIG_PATH = os.path.join(os.path.dirname(os.path.dirname(__file__)), "config.json")


def load_config():
    """
    Loads the configuration file (config.json) and returns the settings as a dictionary.

    Returns:
    -------
    dict:
        Dictionary containing configuration settings.

    Raises:
    ------
    FileNotFoundError:
        If the config.json file is missing.
    JSONDecodeError:
        If the JSON file contains invalid formatting.
    """
    try:
        with open(CONFIG_PATH, "r") as file:
            return json.load(file)
    except FileNotFoundError:
        raise FileNotFoundError(f"Configuration file not found: {CONFIG_PATH}")
    except json.JSONDecodeError:
        raise ValueError(f"Invalid JSON format in {CONFIG_PATH}")


# Load config at module level for quick access
CONFIG = load_config()