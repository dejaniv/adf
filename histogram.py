import matplotlib.pyplot as plt

def showHist(filename):
    with open(filename) as f:
        x = [int(i) for i in f.read()[:-2].split(", ")]
        plt.bar(range(len(x)), x)
        plt.show()

 if __name__ == "__main__":
    if (len(sys.argv) == 2):
        showHist(sys.argv[1])
    else:
        showHist("hist.txt")

