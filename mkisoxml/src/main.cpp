#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <tinyxml2.h>


#define VERSION "0.40"


int ParseDir(tinyxml2::XMLElement *dirElement, tinyxml2::XMLDocument *xmldoc) {

	DIR           *d;
	struct dirent *dir;

	d = opendir(".");

	if (d == 0) {

		printf("ERROR: Unable to open directory.\n");

		return -1;

	}

	{
		char currentDir[MAX_PATH];
		getcwd(currentDir, MAX_PATH);
		dirElement->SetAttribute("srcdir", currentDir);
	}

	while((dir = readdir(d)) != NULL) {

		tinyxml2::XMLElement	*fileElement;
		struct stat				attr;

		stat(dir->d_name, &attr);

		if (S_ISDIR(attr.st_mode)) {

			if (strcasecmp(".", dir->d_name) == 0)
				continue;
			if (strcasecmp("..", dir->d_name) == 0)
				continue;

			fileElement = xmldoc->NewElement("dir");

			fileElement->SetAttribute("name", dir->d_name);

			if (chdir(dir->d_name) != 0) {

				printf("ERROR: Cannot access directory %s.\n", dir->d_name);

				return -1;

			}


			ParseDir(fileElement, xmldoc);

			chdir("..");

			dirElement->InsertEndChild(fileElement);

		} else {

			fileElement = xmldoc->NewElement("file");

            fileElement->SetAttribute("name", dir->d_name);
            fileElement->SetAttribute("type", "data");

			dirElement->InsertEndChild(fileElement);

		}

	}

	closedir(d);

	return 0;

}

char oldDir[MAX_PATH];

int main(int argc, const char* argv[]) {

	if (argc == 1) {

		printf("MKISOXML " VERSION " - XML ISO Generator for MKPSXISO\n");
		printf("2017 Meido-Tek Productions (Lameguy64)\n\n");

		printf("mkisoxml -o <output> <path>\n\n");
		printf("   -o     - Specifies the filename for the XML output.\n");
		printf("   <path> - Path of directory to parse.\n");

		return EXIT_SUCCESS;

	}


	const char *output = 0;
    const char *path = 0;

    for(int i=1; i<argc; i++) {

		if (strcasecmp("-o", argv[i]) == 0) {

			i++;
			output = argv[i];

		} else {

			path = argv[i];

		}

    }


    if (output == 0) {

		printf("No output filename specified.\n");
		return EXIT_FAILURE;

    }

	if (path == 0) {

		printf("No directory path specified.\n");
		return EXIT_FAILURE;

	}

	getcwd(oldDir, MAX_PATH);

	tinyxml2::XMLDocument xmldoc;

    tinyxml2::XMLElement *baseElement = xmldoc.NewElement("iso_project");


    tinyxml2::XMLElement *trackElement = xmldoc.NewElement("track");

    trackElement->SetAttribute("type", "data");

    tinyxml2::XMLElement *dirTreeElement = xmldoc.NewElement("directory_tree");


    if (chdir(path) != 0) {

		printf("ERROR: Unable to parse specified directory.\n");

		return EXIT_FAILURE;

    }

    printf("Parsing directory %s... ", path);

	if (ParseDir(dirTreeElement, &xmldoc) != 0)
		return EXIT_FAILURE;

	trackElement->InsertEndChild(dirTreeElement);

	printf("Ok.\n");

    baseElement->InsertEndChild(trackElement);

    xmldoc.InsertEndChild(baseElement);

    chdir(oldDir);


    if (xmldoc.SaveFile(output) != tinyxml2::XML_SUCCESS) {

		printf("ERROR: Unable to write file %s\n", output);
		return EXIT_FAILURE;

    }

	return 0;

}
