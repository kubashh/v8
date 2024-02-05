import sqlite3

conn = sqlite3.connect('test1.db')
c = conn.cursor()
c.execute("CREATE TABLE movie(title, year, score)")
res = c.execute("SELECT name FROM sqlite_master")

print(res.fetchone())
