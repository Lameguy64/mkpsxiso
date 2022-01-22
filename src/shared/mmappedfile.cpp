#include "mmappedfile.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

MMappedFile::MMappedFile()
	: m_handle(nullptr)
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
	// TODO: Do
#endif
}

bool MMappedFile::Create(const std::filesystem::path& filePath, uint64_t size)
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
	// TODO: Do
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

	const uint64_t mapStartOffset = (offset / allocGranularity) * allocGranularity;
	const uint64_t viewDelta = offset - mapStartOffset;
	size += viewDelta;

	ULARGE_INTEGER ulOffset;
	ulOffset.QuadPart = mapStartOffset;
	m_mapping = MapViewOfFile(reinterpret_cast<HANDLE>(handle), FILE_MAP_ALL_ACCESS, ulOffset.HighPart, ulOffset.LowPart, size);
	if (m_mapping != nullptr)
	{
		m_data = static_cast<char*>(m_mapping) + viewDelta;
	}

#else
	// TODO: Do
#endif
}

MMappedFile::View::~View()
{
#ifdef _WIN32
	if (m_mapping != nullptr)
	{
		UnmapViewOfFile(m_mapping);
	}
#else

#endif
}