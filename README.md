
# Numeraire library

The package aim is to provide a comprehensive set of tools to help in different aspect of pricing plain vanilla european options in Black Scholes framework. Additional module provides that provides also extra module for sensitivity analysis. 

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
To use the package you need to clone the repository first. The package itself has a numerous dependencies and it is better to isolate environment for this module. To run it properly ple
## Dependecies
To run the code smoothly a User must first import QuantLib library and Django. From QuantLib we laverage only calendar schedule and lifecycle of trades. Django provides framework for FrondEnd. All analytical formula are implemented from scratch.

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




