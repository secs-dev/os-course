package itmo.localpiper.vtfs;

import java.util.Optional;

import org.springframework.data.jpa.repository.JpaRepository;

public interface FileMetadataRepository extends JpaRepository<FileMetadata, Long> {
    Optional<FileMetadata> findByFileName(String fileName);
    Optional<FileMetadata> findByInode(long inode);
}
