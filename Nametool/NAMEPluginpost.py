from sklearn.cluster import AffinityPropagation
import numpy as np
import Levenshtein

with open('/Users/mypc/Downloads/PS/NAMEPluginout.txt', 'r') as file:
    content = file.readlines()
    str = [line.strip() for line in content]
print(str)
strlen = len(str)
res = np.zeros((strlen, strlen))
for i in range(0, strlen):
    for j in range(0, strlen):
        res[i][j] = - Levenshtein.distance(str[i], str[j])
print(res)
af = AffinityPropagation(random_state=None, max_iter=200,affinity='euclidean', preference=np.min(res),verbose=True).fit(res)
print(af.labels_)
