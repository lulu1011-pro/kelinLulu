void selectionSort1(vector<int>& a) {
	int n = a.size();
	for (int i = 0; i < n - 1; i++) {
		//
		int maxIndex = i;
		//
		for (int j = i + 1; j < n; j++) {
			if (a[j] > a[maxIndex]) {
				maxIndex = j;
			}
		}
		if (maxIndex != i) {
			swap(a[i], a[maxIndex]);
		}
	}
}
