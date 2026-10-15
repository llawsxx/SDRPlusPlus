import math
import re
import struct
import sys
from pathlib import Path


def generate(array_name, filenames):
    print('#include "defines.h"')
    dimensions = []
    for index, filename in enumerate(filenames):
        text = re.sub(r"#.*", "", Path(filename).read_text())
        values = re.findall(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", text)
        width, count = int(values[0]), int(values[1])
        entries = values[2:]
        if len(entries) != width * count:
            raise ValueError(f"Invalid codebook dimensions: {filename}")
        dimensions.append((width, count))
        print(f"static float codes{index}[] = {{")
        for entry in entries:
            value = struct.unpack("f", struct.pack("f", float(entry)))[0]
            print(f"{value:.6g},")
        print("};")
    print(f"const struct lsp_codebook {array_name}[] = {{")
    for index, (width, count) in enumerate(dimensions):
        print(f"{{{width}, {math.floor(math.log2(count) + 0.5)}, {count}, codes{index}}},")
    print("{0, 0, 0, 0}\n};")


if __name__ == "__main__":
    generate(sys.argv[1], sys.argv[2:])
