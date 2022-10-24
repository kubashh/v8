
import re

CONTRA_RE = re.compile('(.+)->(.+)')


with open('contra') as f: 
  contradictions = f.read()

contr = []

for line in contradictions.strip().splitlines():
  line = line.strip()
  match = CONTRA_RE.match(line)
  premise = match.group(1)
  implication = match.group(2)
  contr.append((premise, implication))

print(contr)