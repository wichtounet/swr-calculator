import requests
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

def print_full(x):
    pd.set_option('display.max_rows', len(x))
    print(x)
    pd.reset_option('display.max_rows')

# Define the URL
baseurl = "http://localhost:8085/api/simple?inflation=us_inflation&initial=1000&years=30&wr=4.0&end=2022"

#grab some years that give a full 30 range
years = range(1970, 2020, 3)
portfolios = ["us_stocks:80;us_bonds:20;", "us_stocks:50;us_bonds:50;", "us_stocks:20;us_bonds:80;"]
balanced= ["none", "monthly", "yearly"]

results = []

for balanceStrategy in balanced:
    for portfolio in portfolios:
        for year in years:
            year = str(year)
            url = baseurl+"&start="+year+"&rebalance="+balanceStrategy+"&portfolio="+portfolio
            # Make a GET request to the URL
            response = requests.get(url)
            if response.status_code == 200:
             # Parse the response JSON
                swr_data = response.json()
                result = {}
                result["year"]=year[2:]
                result["portfolio"]=portfolio
                result["balanced"]=balanceStrategy
                #use remaining total value average
                result["tv_median"] =  swr_data['results']['tv_median']
                results.append(result)
            else:
                print("Error:", response.status_code)    

df = pd.DataFrame(results)

# Create line plot that groups on portfolio and facets on rebalance strategy
sns.set(style="whitegrid")
g = sns.FacetGrid(df, col="balanced", col_wrap=3, height=4)
g.map_dataframe(sns.lineplot, "year", "tv_median", hue="portfolio", marker="o")
g.set_axis_labels("Year", "TV Median")
g.add_legend()
plt.show()



