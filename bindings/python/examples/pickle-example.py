import pickle, io
import diy

domain = diy.DiscreteBounds([0,0,0], [100, 100, 100])
d = diy.Direction([-1,0,1])

f = io.BytesIO()
pickle.dump(domain.max, f)
pickle.dump(domain, f)
pickle.dump(d, f)

f.seek(0)
print(pickle.load(f))
print(pickle.load(f))
print(pickle.load(f))
