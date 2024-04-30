import requests
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt
import sys

def print_full(x):
    pd.set_option('display.max_rows', len(x))
    print(x)
    pd.reset_option('display.max_rows')

#Default server and port if needed
server = "localhost"
port = "8085"
inflation="us_inflation"
initial = "1000"
years = "30"
wr="4.0"
start = "1960"
end = "2020"
step = "5"

if len(sys.argv) >=10:
    server = sys.argv[1]
    port = sys.argv[2]
    inflation = sys.argv[3]
    initial = sys.argv[4]
    years = sys.argv[5]
    wr = sys.argv[6]
    start = sys.argv[7]
    end = sys.argv[8]
    step = sys.argv[9]    

else:
    print("args invalid using default >> balancing_analysis.py localhost 8085 us_inflation 1000 30 4.0 1960 2020 5")    

# Define the URL
baseurl = "http://"+server+":"+port+"/api/simple?inflation="+inflation+"&initial="+initial+"&years="+years+"&wr="+wr+"&end="+end

#based on the inputs with step years
years = range(int(start), int(end), int(step))
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
g = sns.FacetGrid(df, col="portfolio", col_wrap=3, height=4)
g.map_dataframe(sns.lineplot, "year", "tv_median", hue="balanced", marker="o")
g.set_axis_labels("Year", "Median Term Val")
g.add_legend()
plt.show()



