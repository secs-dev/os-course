package itmo.localpiper.vtfs;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

@Service
public class FileMetadataService {

    @Autowired
    private FileMetadataRepository fileMetadataRepository;

    public FileMetadata createFile(String fileName) {
        FileMetadata fileMetadata = new FileMetadata();
        fileMetadata.setFileName(fileName);
        fileMetadata.setInode(generateInode());
        fileMetadata.setLinkCount(1);
        return fileMetadataRepository.save(fileMetadata);
    }

    public FileMetadata linkFile(String oldFileName, String newFileName) {
        FileMetadata oldFile = fileMetadataRepository.findByFileName(oldFileName)
                .orElseThrow(() -> new RuntimeException("File not found"));
        FileMetadata newFile = new FileMetadata();
        newFile.setFileName(newFileName);
        newFile.setInode(oldFile.getInode());
        newFile.setLinkCount(oldFile.getLinkCount() + 1);
        
        fileMetadataRepository.save(newFile);
        
        oldFile.setLinkCount(oldFile.getLinkCount() + 1);
        fileMetadataRepository.save(oldFile);
        
        return newFile;
    }

    public void deleteFile(String fileName) {
        FileMetadata file = fileMetadataRepository.findByFileName(fileName)
                .orElseThrow(() -> new RuntimeException("File not found"));

        if (file.getLinkCount() > 1) {
            file.setLinkCount(file.getLinkCount() - 1);
            fileMetadataRepository.save(file);
        } else {
            fileMetadataRepository.delete(file);
        }
    }

    private long generateInode() {
        return System.currentTimeMillis();
    }
}

