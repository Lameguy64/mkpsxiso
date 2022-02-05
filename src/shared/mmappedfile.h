#pragma once

// Cross-platform memory mapped file wrapper

#include <cstdint>
#include "fs.h"

class MMappedFile
{
public:
	class View
	{
	public:
		View(void* handle, uint64_t offset, size_t size);
		~View();

		void* GetBuffer() const { return m_data; }

	private:
		void* m_mapping = nullptr; // Aligned down to allocation granularity
		void* m_data = nullptr;
		size_t m_size = 0; // Real size, with adjustments to granularity
	};

	MMappedFile();
	~MMappedFile();

	bool Create(const fs::path& filePath, uint64_t size);
	View GetView(uint64_t offset, size_t size) const;

private:
	void* m_handle = nullptr; // Opaque, platform-specific
};