#include <gflags/gflags.h>
#include <sstream>

#include "src/lib/metrics.h"
#include "src/lib/debug.h"
#include "src/lib/threadsafe_progress_indicator.h"

#include "src/codesearch.h"
#include "src/git_indexer.h"
#include "src/smart_git.h"
#include "src/score.h"

#include "src/proto/config.pb.h"

using namespace std;

DEFINE_string(order_root, "", "Walk top-level directories in this order.");
DEFINE_bool(revparse, false, "Display parsed revisions, rather than as-provided");

git_indexer::git_indexer(code_searcher *cs,
                         const google::protobuf::RepeatedPtrField<RepoSpec>& repositories)
    : cs_(cs), repositories_to_index_(repositories) {
    int err;
    if ((err = git_libgit2_init()) < 0)
        die("git_libgit2_init: %s", giterr_last()->message);

    git_libgit2_opts(GIT_OPT_SET_CACHE_OBJECT_LIMIT, GIT_OBJ_BLOB, 10*1024);
    git_libgit2_opts(GIT_OPT_SET_CACHE_OBJECT_LIMIT, GIT_OBJ_OFS_DELTA, 10*1024);
    git_libgit2_opts(GIT_OPT_SET_CACHE_OBJECT_LIMIT, GIT_OBJ_REF_DELTA, 10*1024);
}

git_indexer::~git_indexer() {
    git_libgit2_shutdown();
}

void git_indexer::print_last_git_err_and_exit(int err) {
    const git_error *e = giterr_last();
    printf("Error %d/%d: %s\n", err, e->klass, e->message);
    exit(1);
}

// will be called by a thread to walk a subsection of repositories_to_index_
// we then take the threads local `files_to_index_local` and append it to the
// global `files_to_index_`.
// we then call `index_files` to actaully submit the files to be indexed by
// codesearch
void git_indexer::walk_repositories_subset(int start, int end, threadsafe_progress_indicator *tpi) {

    std::vector<std::unique_ptr<pre_indexed_file>> files_to_index_local;
    files_to_index_local.reserve((end - start) * 100); // 100 repos per
    /* fprintf(stderr, "walk_repositories_subset: %d-%d\n", start, end); */
    for (int i = start; i < end; ++i) {
        const auto &repo = repositories_to_index_[i];
        const char *repopath = repo.path().c_str();

        /* fprintf(stderr, "walking repo: %s\n", repopath); */
        git_repository *curr_repo = NULL;

        // Is it safe to assume these are bare repos (or the mirror clones)
        // that we create?. If so, we can use git_repository_open_bare
        int err = git_repository_open_bare(&curr_repo, repopath);
        if (err < 0) {
            print_last_git_err_and_exit(err);
        }

        for (auto &rev : repo.revisions()) {
            /* fprintf(stderr, "walking %s at %s \n", repopath, rev.c_str()); */
            walk(curr_repo, rev, repo.path(), repo.name(), repo.metadata(), repo.walk_submodules(), "", files_to_index_local);
            /* fprintf(stderr, "done walking %s at %s\n", repopath, rev.c_str()); */
            tpi->tick();
        }

        /* git_repository_free(curr_repo); */
    }

    // Then at the end we'll post to files_to_index_
    // Then we're done
    /* fprintf(stderr, "trying to do our wild mutex backed append\n"); */
    std::lock_guard<std::mutex> guard(files_mutex_);
    files_to_index_.insert(files_to_index_.end(), std::make_move_iterator(files_to_index_local.begin()), std::make_move_iterator(files_to_index_local.end()));
}

void git_indexer::begin_indexing() {

    // min_per_thread will require tweaking. For example, even with only
    // 2 repos, would it not be worth it to spin up two threads (if available)?
    // Or would the overhead of the thread creation far outweigh the single-core
    // performance for just a few repos.
    
    unsigned long const length = repositories_to_index_.size();
    unsigned long const min_per_thread = 16; 
    unsigned long const max_threads = 
        (length + min_per_thread - 1)/min_per_thread;
    unsigned long const hardware_threads = std::thread::hardware_concurrency();
    unsigned long const num_threads = std::min(hardware_threads!=0?hardware_threads:2, max_threads);
    // We round block size up. the last thread may have less items to work with
    // when length is uneven
    unsigned long const block_size = (length + num_threads - 1) / (num_threads);

    fprintf(stderr, "length=%lu min_per_thread=%lu max_threads=%lu hardware_threads=%lu num_threads=%lu block_size=%lu\n", length, min_per_thread, max_threads, hardware_threads, num_threads, block_size);

    threadsafe_progress_indicator tpi(length, "Walking repos...", "Done");

    if (length < 2 * min_per_thread) {
        fprintf(stderr, "Not going to create any new threads.\n");
        walk_repositories_subset(0, length, &tpi);
        index_files();
        return;
    }

    threads_.reserve(num_threads - 1);
    long block_start = 0;
    for (long i = 0; i < num_threads; ++i) {
        long block_end = std::min(block_start + block_size, length);
        threads_.emplace_back(&git_indexer::walk_repositories_subset, this, (int)block_start, (int)block_end, &tpi);
        block_start = block_end;
    }

    for (long i = 0; i < num_threads; ++i) {
        threads_[i].join();
    }

    index_files();
}

/* bool compareFiles(pre_indexed_file a, pre_indexed_file b) { */
/*     return a.score > b.score; */
/* } */

// sorts `files_to_index_` based on score. This way, the lowest scoring files
// get indexed last, and so show up in sorts results last. We use a stable sort
// so that most of a repos files are indexed together which has 2 benefits:
//  1. If a term is matched in a lot of repos files, that repos files will end
//     up grouped together in search results
//  2. We can avoid many calls to git_repository_open/git_repository_free. At
//     least until the end, where many repos "low" scoring files are found.
// walks `files_to_index_`, looks up the repo & blob combination for each file
// and then calls `cs->index_file` to actually index the file.
void git_indexer::index_files() {
    fprintf(stderr, "sorting files_to_index_... [%lu]\n", files_to_index_.size());
    /* std::stable_sort(files_to_index_.begin(), files_to_index_.end(), compareFiles); */
    fprintf(stderr, "  done\n");

    /* fprintf(stderr, "walking files_to_index_ ...\n"); */
    threadsafe_progress_indicator tpi(files_to_index_.size(), "Indexing files_to_index_...", "Done");
    for (auto it = files_to_index_.begin(); it != files_to_index_.end(); ++it) {
        auto file = it->get();

        /* const char *repopath = file.repopath.c_str(); */

        /* fprintf(stderr, "indexing %s/%s\n", repopath, file.path.c_str()); */
        
        const char *data = static_cast<const char*>(git_blob_rawcontent(file->blob));
        cs_->index_file(file->tree, file->path, StringPiece(data, git_blob_rawsize(file->blob)));

        git_blob_free(file->blob);
        tpi.tick();
    }
}

void git_indexer::walk(git_repository *curr_repo,
        const string& ref, 
        const string& repopath, 
        const string& name,
        Metadata metadata,
        bool walk_submodules,
        const string& submodule_prefix,
        std::vector<std::unique_ptr<pre_indexed_file>>& results) {
    smart_object<git_commit> commit;
    smart_object<git_tree> tree;
    if (0 != git_revparse_single(commit, curr_repo, (ref + "^0").c_str())) {
        fprintf(stderr, "%s: ref %s not found, skipping (empty repo?)\n", name.c_str(), ref.c_str());
        return;
    }
    git_commit_tree(tree, commit);
    /* fprintf(stderr, "opened commit_tree for %s\n", repopath.c_str()); */

    char oidstr[GIT_OID_HEXSZ+1];
    string version = FLAGS_revparse ?
        strdup(git_oid_tostr(oidstr, sizeof(oidstr), git_commit_id(commit))) : ref;

    const indexed_tree *idx_tree = cs_->open_tree(name, metadata, version);
    walk_tree("", FLAGS_order_root, repopath, walk_submodules, submodule_prefix, idx_tree, tree, curr_repo, results);
}


void git_indexer::walk_tree(const string& pfx,
                            const string& order,
                            const string& repopath,
                            bool walk_submodules,
                            const string& submodule_prefix,
                            const indexed_tree *idx_tree,
                            git_tree *tree,
                            git_repository *curr_repo,
                            std::vector<std::unique_ptr<pre_indexed_file>>& results) {
    /* fprintf(stderr, "preparing to walk_tree for %s with prefix: %s \n", repopath.c_str(), pfx.c_str()); */
    map<string, const git_tree_entry *> root;
    vector<const git_tree_entry *> ordered;
    int entries = git_tree_entrycount(tree);
    /* fprintf(stderr, "repo: %s has %d entries\n", repopath.c_str(), entries); */
    for (int i = 0; i < entries; ++i) {
        const git_tree_entry *ent = git_tree_entry_byindex(tree, i);
        root[git_tree_entry_name(ent)] = ent;
    }

    istringstream stream(order);
    string dir;
    while(stream >> dir) {
        map<string, const git_tree_entry *>::iterator it = root.find(dir);
        if (it == root.end())
            continue;
        ordered.push_back(it->second);
        root.erase(it);
    }
    for (map<string, const git_tree_entry *>::iterator it = root.begin();
         it != root.end(); ++it)
        ordered.push_back(it->second);
    for (vector<const git_tree_entry *>::iterator it = ordered.begin();
         it != ordered.end(); ++it) {
        
        // We manually free this object in index_files()
        git_object *obj;
        git_tree_entry_to_object(&obj, curr_repo, *it);
        string path = pfx + git_tree_entry_name(*it);

        /* fprintf(stderr, "walking obj with path: %s/%s\n", repopath.c_str(), path.c_str()); */

        if (git_tree_entry_type(*it) == GIT_OBJ_TREE) {
            walk_tree(path + "/", "", repopath, walk_submodules, submodule_prefix, idx_tree, (git_tree*)obj, curr_repo, results);
        } else if (git_tree_entry_type(*it) == GIT_OBJ_BLOB) {
            const string full_path = submodule_prefix + path;
            auto file = std::make_unique<pre_indexed_file>();

            file->tree = idx_tree;
            file->repopath = repopath;
            file->path = path;
            file->score = score_file(full_path);
            file->repo = curr_repo;
            file->blob = (git_blob*)obj;

            /* fprintf(stderr, "indexing %s/%s\n", repopath.c_str(), file.path.c_str()); */
            /* if (!files_to_index_local.get()) { */
            /*     files_to_index_local.put(new vector<pre_indexed_file>()); */
            /*     files_to_index_local.get()->reserve(100); */
            /* } */
            results.push_back(std::move(file));

        } else if (git_tree_entry_type(*it) == GIT_OBJ_COMMIT) {
            // Submodule
            if (!walk_submodules) {
                continue;
            }
            git_submodule* submod = nullptr;
            if (0 != git_submodule_lookup(&submod, curr_repo, path.c_str())) {
                fprintf(stderr, "Unable to get submodule entry for %s, skipping\n", path.c_str());
                continue;
            }

            const char* sub_name = git_submodule_name(submod);
            string sub_repopath = repopath + "/" + path;
            string new_submodule_prefix = submodule_prefix + path + "/";
            Metadata meta;

            const git_oid* rev = git_tree_entry_id(*it);
            char revstr[GIT_OID_HEXSZ + 1];
            git_oid_tostr(revstr, GIT_OID_HEXSZ + 1, rev);

            // Open the submodule repo
            git_repository *sub_repo;

            int err = git_repository_open_bare(&sub_repo, sub_repopath.c_str());

            if (err < 0) {
                fprintf(stderr, "Unable to open subrepo: %s\n", sub_repopath.c_str());
                print_last_git_err_and_exit(err);
            }

            walk(sub_repo, string(revstr), sub_repopath, string(sub_name), meta, walk_submodules, new_submodule_prefix, results);

            git_repository_free(sub_repo);
        }
    }
}
