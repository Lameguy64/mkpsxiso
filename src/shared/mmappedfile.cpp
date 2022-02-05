#include "mmappedfile.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#endif

MMappedFile::MMappedFile()
{
}

MMappedFile::~MMappedFile()
{
#ifdef _WIN32
	if (m_handle != nullptr)
	{
		CloseHandle(reinterpret_cast<HANDLE>(m_handle));
	}
#else
	if (m_handle != nullptr)
	{
		close(static_cast<int>(reinterpret_cast<intptr_t>(m_handle)));
	}
#endif
}

bool MMappedFile::Create(const fs::path& filePath, uint64_t size)
{
	bool result = false;

#ifdef _WIN32
	HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE)
	{
		ULARGE_INTEGER ulSize;
		ulSize.QuadPart = size;

		HANDLE fileMapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, ulSize.HighPart, ulSize.LowPart, nullptr);
		if (fileMapping != nullptr)
		{
			m_handle = fileMapping;
			result = true;
		}

		CloseHandle(file);
	}
#else
	int file = open(filePath.c_str(), O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	if (file != -1)
	{
		if (ftruncate(file, size) == 0)
		{
			m_handle = reinterpret_cast<void*>(file);
			result = true;
		}
		else
		{
			close(file);
		}
	}
#endif
	return result;
}

MMappedFile::View MMappedFile::GetView(uint64_t offset, size_t size) const
{
	return View(m_handle, offset, size);
}

MMappedFile::View::View(void* handle, uint64_t offset, size_t size)
{
#ifdef _WIN32
	SYSTEM_INFO SysInfo;
	GetSystemInfo(&SysInfo);
	const DWORD allocGranularity = SysInfo.dwAllocationGranularity;
#else
	const long allocGranularity = sysconf(_SC_PAGE_SIZE);
#endif

	const uint64_t mapStartOffset = (offset / allocGranularity) * allocGranularity;
	const uint64_t viewDelta = offset - mapStartOffset;
	size += viewDelta;

#ifdef _WIN32
	ULARGE_INTEGER ulOffset;
	ulOffset.QuadPart = mapStartOffset;
	void* mapping = MapViewOfFile(reinterpret_cast<HANDLE>(handle), FILE_MAP_ALL_ACCESS, ulOffset.HighPart, ulOffset.LowPart, size);
	if (mapping != nullptr)
#else
	void* mapping = mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_SHARED, static_cast<int>(reinterpret_cast<intptr_t>(handle)), mapStartOffset);
	if (mapping != MAP_FAILED)
#endif
	{
		m_mapping = mapping;
		m_data = static_cast<char*>(m_mapping) + viewDelta;
		m_size = size;
	}
}

MMappedFile::View::~View()
{
#ifdef _WIN32
	if (m_mapping != nullptr)
	{
		UnmapViewOfFile(m_mapping);
	}
#else
	if (m_mapping != nullptr)
	{
		munmap(m_mapping, m_size);
	}
#endif
}