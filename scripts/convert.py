import csv
import sys

def main():
    file_path = sys.argv[1]
    search_column = sys.argv[2]

    with open(file_path, "r") as csvfile:
        reader = csv.reader(csvfile, delimiter=',')
        next(reader) # Drop this line
        next(reader) # Drop this line
        next(reader) # Drop this line
        next(reader) # Drop this line
        columns = next(reader)

        column_index = -1
        for i, arg in enumerate(columns):
            if arg == search_column:
                column_index = i
                break

        if column_index == -1:
            print("Did not find the column")
            exit(1)

        for row in reader:
            if not row[column_index]:
                break

            value = row[column_index]
            value = value.replace('.', ',')
            value = ".".join(value.rsplit(',', 1))
            number = "{}".format(value).replace(',', '')
            print("{},{},{}".format(row[0],row[1], number))

if __name__ == "__main__":
    main()
