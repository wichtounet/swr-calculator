#!/bin/python3

import csv
import sys

def main():
    file_path = sys.argv[1]
    house_type = sys.argv[2]
    metric = sys.argv[3]

    with open(file_path, "r") as csvfile:
        reader = csv.reader(csvfile, delimiter=';')
        next(reader) # Drop this line
        next(reader) # Drop this line
        next(reader) # Drop this line
        columns = next(reader)

        date_column = columns.index("Date")
        d0_column = columns.index("D0")
        d1_column = columns.index("D1")
        value_column = columns.index("Value")

        previous_value = 0

        for row in reader:
            date = row[date_column]
            year = int(date.split('-')[0])
            quarter = int(date.split('-')[1][1])
            d0 = row[d0_column]
            d1 = row[d1_column]
            value = row[value_column]

            if len(value) == 0:
                continue

            number = float(value)
            month = (quarter - 1) * 3 + 1

            year_m = year - 1 if quarter == 1 else year
            month_m1 = 12 if quarter == 1 else month - 1
            month_m2 = month_m1 - 1

            if d0 == house_type and d1 == metric:
                if previous_value != 0:
                    diff = number - previous_value
                    step = diff / 3.0
                    number_m2 = previous_value + 1.0 * step
                    number_m1 = previous_value + 2.0 * step
                    print(f"{month_m2},{year_m},{number_m2}")
                    print(f"{month_m1},{year_m},{number_m1}")

                print(f"{month},{year},{number}")
                previous_value = number

if __name__ == "__main__":
    main()
