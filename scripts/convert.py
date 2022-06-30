import csv
import sys

def main():
    file_path = sys.argv[1]
    search_column = sys.argv[2]

    with open(file_path) as csvfile:
        reader = csv.reader(csvfile)
        next(reader) # Drop this line
        next(reader) # Drop this line
        next(reader) # Drop this line
        next(reader) # Drop this line
        columns = next(reader)

        column = -1
        for i, arg in enumerate(columns):
            if arg == search_column:
                column = i
                break

        if column == -1:
            print("Did not find the column")
            exit(1)

        for row in reader:
            if not row[column]:
                break

            number = "{}".format(row[column]).replace(',','')
            print("{},{},{}".format(row[0],row[1], number))

if __name__ == "__main__":
    main()
