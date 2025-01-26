package itmo.localpiper.vtfs;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api/files")
public class FileMetadataController {

    @Autowired
    private FileMetadataService fileService;

    @PostMapping("/create")
    public ResponseEntity<FileMetadata> createFile(@RequestParam String fileName) {
        FileMetadata file = fileService.createFile(fileName);
        return new ResponseEntity<>(file, HttpStatus.CREATED);
    }

    @PostMapping("/link")
    public ResponseEntity<FileMetadata> linkFile(@RequestParam String oldFileName, @RequestParam String newFileName) {
        FileMetadata file = fileService.linkFile(oldFileName, newFileName);
        return new ResponseEntity<>(file, HttpStatus.CREATED);
    }

    @DeleteMapping("/delete")
    public ResponseEntity<Void> deleteFile(@RequestParam String fileName) {
        fileService.deleteFile(fileName);
        return new ResponseEntity<>(HttpStatus.NO_CONTENT);
    }
}

