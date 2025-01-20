
# Numeraire library

The application's aim is to provide a comprehensive set of tools to help in different aspect of pricing  derivative product
Additional module provides that provides also extra module for sensitivity analysis. 

#### Table of contents
[Instalation](#Instalation)  
[Project Structure](#ProjectStructure)  
[General Overview](#GeneralOverview)  
[Configuration](#Configuration)  
[Underlier Simulation](#UnderlierSimulation)  
[Black-Scholes Pricing Framework](#BlackScholesPricingFramework)  
[Sensitivity Analysis](#SensitivityAnalysis)  
[Utils](#Utils)  

## Instalation
To use the package you need to clone the repository first. The package itself has a numerous dependencies and it is
better to isolate environment for this module. 
## Dependecies
To run the code smoothly a User must first import QuantLib library and Django Tailwind. From QuantLib we leverage only calendar 
schedule and lifecycle of trades. Django provides framework for FrondEnd. All analytical formula are implemented from
scratch.



## Structure
```
Numeraire Project
--base
--static
--templates
--tool_kit
--theme
--venv
--wiener
    |
    |--src/wiener
        |
        |-analytical_method.py
        |-pricing_environment.py
        |-mont_carlo_methods.py


    

```
## App launching
To run application first run follwing command in terminal

```
python manage.py tailwind start

```

Then we run django dev server by running command:
```
python manage.py runserver

```

# YahooData Extractor
## Overview
The YahooDataExtractor class is a Python utility designed for extracting financial data from Yahoo Finance. This class is particularly useful for financial analysts, data scientists, and developers working with historical market data. Below is an overview of its functionality and design.

## Key futures
### 1. Initialization:

The class is initialized with the following parameters:

 * ```tickers```: A single ticker (string) or a list of tickers (e.g., "TSLA" or \["AAPL", "GOOG"]).

* ```start_period```: The start date for data extraction (in "YYYY-MM-DD" format).

* ```end_period```: The end date for data extraction. If set to "newest", the class retrieves the latest available price.

### 2. Data Extraction:

* The ```extract_data``` method dynamically adapts to handle different scenarios:

* Multiple Tickers: Loops through the tickers and downloads their data over the specified date range.

* Single Ticker with Time Stamp: Retrieves the price for a specific day or the newest available price since the start date.

* The extracted data is stored in a dictionary, where each key is a ticker, and the value is either a DataFrame or a specific price.

### 3. Analysis and Reporting:

* The class provides static methods for basic descriptive and structural analysis of the data:

* ```df_info```: Displays statistical summaries like mean, standard deviation, and percentiles.

* ```basic_statistic```: Outputs column-level metadata, aiding in data integrity checks.

## Possible Improvements
*	 Add support for additional columns or metrics (e.g., volume, high, low).
*	 Extend error handling to address edge cases, such as invalid tickers or missing data.
*	 Provide built-in visualization options for extracted time series data.
* Integrate caching mechanisms to avoid redundant API calls for frequently accessed data.

## Potential Applications
*  Market Analysis : Quickly retrieve and analyze historical price trends.
*  Algorithmic Trading : Use the extracted data as input for backtesting and developing trading algorithms.
*  Portfolio Management : Assess performance metrics for a set of assets over a specific time frame.
*  Academic Research : Extract and analyze financial data for research purposes.

## Summary
This class is a robust foundation for financial data extraction and serves as a useful tool for handling diverse financial datasets.

# QuantLibToolKit 
### Overview
The **QuantLibToolKit** is a utility class that provides a set of static methods for working with QuantLib’s date-related and scheduling functionalities. It simplifies the creation and manipulation of financial schedules, calendars, date adjustments, and year fractions.
### Key features include:

#### Date Conversion:
*	 Convert Python datetime.date objects or string representations into QuantLib date objects.
#### Calendar Setup:
* Define financial calendars for supported regions (e.g., USA, UK, Switzerland, Poland).
#### Date Adjustment Rules:
 *	 Apply day-count conventions to adjust invalid dates (e.g., holidays, weekends) to valid business days.
#### Frequency and Year Fraction:
*	Configure date frequencies for financial schedules (e.g., daily, monthly, quarterly).
*	Select and compute year fraction conventions (e.g., Actual/360, Actual/365).
#### Schedule Definition:
*	Create complex date schedules with options for frequency, calendars, and adjustment rules.
*	Display and compute consecutive or cumulative year fractions for a given schedule.

This class is highly modular and provides a user-friendly abstraction over QuantLib’s low-level API, making it suitable for building financial applications like bond pricing, derivative pricing, or risk management tools.

# Diagram
```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
    'background': 'rgb(245,245,245)', 
    'primaryColor': 'rgb(255,255,255)', 
    'primaryTextColor': '#333333', 
    'edgeLabelBackground': '#ffffff',
    'tertiaryColor': '#f5f5f5',
    'fontFamily': 'Arial'
}}}%%
classDiagram
    class QuantLibToolKit {
        <<static>> dict _weekday_corrections
        <<static>> dict _date_generation_rules
        <<static>> date_object_2ql_date(date: datetime.date) ql.Date
        <<static>> string_2ql_date(date: str) ql.Date
        <<static>> set_calendar(country: str="theUK") ql.Calendar
        <<static>> set_date_corrections_schema(corrections_map: dict, correction_rule: str="Following") int
        <<static>> set_rule_of_date_generation(date_generation_rules: str="forward") int
        <<static>> set_frequency(freq_period: str) int
        <<static>> set_year_fraction_convention(year_fraction_conv: str="Actual365") ql.FractionConvention
        <<static>> define_schedule(valuation_date: str, termination_date: str, freq_period: str="monthly",calendar: str="theUK", correction_rule: str="Following") ql.Schedule
        <<static>> display_schedule(schedule: ql.Schedule) void
        <<static>> consecutive_year_fraction(schedule: ql.Schedule, day_count: ql.DayCounter=None) List~float~
        <<static>> cumulative_year_fraction(schedule: ql.Schedule, day_count: ql.DayCounter=None) List~float~
    }
 
       %% Placeholder for external classes
    class ql_Date {
        <<external>> ql.Date
    }

    class ql_Schedule {
        <<external>> ql.Schedule
    }

    class ql_Calendar {
        <<external>> ql.Calendar
    }

    class ql_FractionConvention {
        <<external>> ql.FractionConvention
    }

    class ql_DayCounter {
        <<external>> ql.DayCounter
    }

    %% Dependencies
    QuantLibToolKit --> ql_Date : Uses
    QuantLibToolKit --> ql_Schedule : Uses
    QuantLibToolKit --> ql_Calendar : Uses
    QuantLibToolKit --> ql_FractionConvention : Uses
    QuantLibToolKit --> ql_DayCounter : Uses
``` 






