import requests
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

# Define the URL
baseurl = "http://localhost:8085/api/simple?inflation=us_inflation&initial=1000&years=30&wr=4.0&end=2022"


years = ["1970","1971","1972","1973","1974","1975","1976","1977","1978","1979"]
portfolios = ["us_stocks:80;us_bonds:20;", "us_stocks:50;us_bonds:50;", "us_stocks:20;us_bonds:80;"]
balanced= ["none", "monthly", "yearly"]

results = []

for balanceStrategy in balanced:
    for portfolio in portfolios:
        for year in years:
            url = baseurl+"&start="+year+"&rebalance="+balanceStrategy+"&portfolio="+portfolio
            # Make a GET request to the URL
            response = requests.get(url)
            if response.status_code == 200:
             # Parse the response JSON
                swr_data = response.json()
                result = {}
                result["year"]=year
                result["portfolio"]=portfolio
                result["balanced"]=balanceStrategy
                result["tv_average"] =  swr_data['results']['tv_average']
                results.append(result)
            else:
                print("Error:", response.status_code)    

df = pd.DataFrame(results)

# Create line plot with facets
sns.set(style="whitegrid")
g = sns.FacetGrid(df, col="balanced", col_wrap=3, height=4)
g.map_dataframe(sns.lineplot, "year", "tv_average", hue="portfolio", marker="o")
g.set_axis_labels("Year", "TV Average")
g.add_legend()
plt.show()


