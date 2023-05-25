# -*- coding: utf-8 -*-
"""
Created on Wed May 24 19:14:56 2023

@author: not-skynet
"""

import os
from pathlib import Path
import pandas as pd

# Define directory where python script is executing
scriptDir = Path(__file__ + '../..').resolve()

# Define as wd and move up 1
os.chdir(scriptDir)
os.chdir('../')
csvDir = str(os.getcwd()) + '//stock-data'

# Import csv files based on top directory
df_cash = pd.read_csv(csvDir + '//cash.csv', 
                      names = ['Month', 'Year', 'Value'])
df_chInflation = pd.read_csv(csvDir + '//ch_inflation.csv',
                             names = ['Month', 'Year', 'Value'])
df_chStocks = pd.read_csv(csvDir + '//ch_stocks.csv',
                          names = ['Month', 'Year', 'Value'])
df_gold = pd.read_csv(csvDir + '//gold.csv',
                      names = ['Month', 'Year', 'Value'])
df_usBonds = pd.read_csv(csvDir + '//us_bonds.csv',
                         names = ['Month', 'Year', 'Value'])
df_usInflation = pd.read_csv(csvDir + '//us_inflation.csv',
                             names = ['Month', 'Year', 'Value'])
df_usStocks = pd.read_csv(csvDir + '//us_stocks.csv',
                          names = ['Month', 'Year', 'Value'])
df_usd_ch = pd.read_csv(csvDir + '//usd_chf.csv',
                        names = ['Month', 'Year', 'Value'])

# Calculate difference from previous month
df_list = [df_cash, df_chInflation, df_chStocks, df_gold, df_usBonds,
           df_usInflation, df_usStocks, df_usd_ch]
for i in df_list:
    i['temp'] = i['Value'].shift(1)
    i['diffNum'] = i['Value'] - i['temp']
    i['diffPcnt'] = i['diffNum'] / i['temp'] * 100
