// Add this at the top of scripts.js
function testCreateAccount() {
    console.log("Test create account function called!");
    alert("Create account button works!");
}

// User Authentication Functions
// -----------------------------

// Check if a user is logged in
function isLoggedIn() {
    return sessionStorage.getItem('currentUser') !== null;
}

// Get the current logged in user
function getCurrentUser() {
    const userJson = sessionStorage.getItem('currentUser');
    return userJson ? JSON.parse(userJson) : null;
}

// Logout user
function logout() {
    sessionStorage.removeItem('currentUser');
    sessionStorage.removeItem('currentBook');
    window.location.href = 'index.html';
}

// Login user
function loginUser(username, password, userType) {
    fetch('/api/users')
        .then(response => response.json())
        .then(data => {
            const users = data.users || [];
            let foundUser = null;
            
            // Find user based on type
            if (userType === 'staff') {
                foundUser = users.find(user => 
                    user.type === 'staff' && 
                    user.username === username && 
                    user.password === password
                );
            } else {
                foundUser = users.find(user => 
                    user.type === 'student' && 
                    user.studentId === username && 
                    user.password === password
                );
            }
            
            if (foundUser) {
                // Store user info in sessionStorage
                sessionStorage.setItem('currentUser', JSON.stringify(foundUser));
                
                // Redirect based on user type
                if (foundUser.type === 'staff') {
                    window.location.href = 'admin.html';
                } else {
                    window.location.href = 'student.html';
                }
            } else {
                alert('Invalid username, password, or user type');
            }
        })
        .catch(error => {
            console.error('Error during login:', error);
            alert('Error during login. Please try again.');
        });
}

// Setup tab navigation
function setupTabs() {
    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabContents = document.querySelectorAll('.tab-content');
    
    tabButtons.forEach(button => {
        button.addEventListener('click', function() {
            // Remove active class from all buttons and contents
            tabButtons.forEach(btn => btn.classList.remove('active'));
            tabContents.forEach(content => content.classList.remove('active'));
            
            // Add active class to clicked button and corresponding content
            button.classList.add('active');
            const tabId = button.getAttribute('data-tab');
            document.getElementById(tabId).classList.add('active');
        });
    });
}

// RFID Card Handling Functions
// -----------------------------

// Flag to indicate when we're in book return mode
let inReturnMode = false;

// Poll for card scan - this needs to check less frequently to match the ESP32's temporary UID storage
function pollForCardScan() {
    const cardStatus = document.getElementById('card-status');
    let lastTimestamp = 0;
    
    setInterval(function() {
        fetch('/api/scan')
            .then(response => response.json())
            .then(data => {
                // Only process if the card UID exists and is new (by timestamp)
                if (data && data.uid && data.uid !== "" && data.timestamp > lastTimestamp) {
                    const uidDisplay = document.getElementById('last-uid-display');
                    if (uidDisplay) {
                        uidDisplay.textContent = data.uid;
                    }
                    
                    if (cardStatus) {
                        cardStatus.innerHTML = `Card detected: <strong>${data.uid}</strong>`;
                    }
                    
                    // Update last timestamp
                    lastTimestamp = data.timestamp;
                    
                    // Process card scan only if we're not in return mode
                    if (!inReturnMode) {
                        processCardScan(data.uid);
                    }
                }
            })
            .catch(error => {
                console.log('Error checking for card scan:', error);
            });
    }, 2000); // Check every 2 seconds
}

// Process card scan
function processCardScan(uid) {
    console.log("Processing card with UID:", uid);
    // Check if it's a user card
    fetch('/api/users')
        .then(response => response.json())
        .then(data => {
            const users = data.users || [];
            const user = users.find(u => u.cardUid === uid);
            
            if (user) {
                console.log("Found user:", user);
                // User card found
                sessionStorage.setItem('currentUser', JSON.stringify(user));
                
                if (user.type === 'staff') {
                    window.location.href = 'admin.html';
                } else {
                    window.location.href = 'student.html';
                }
                return true;
            }
            return false;
        })
        .then(userFound => {
            if (!userFound) {
                // Check if it's a book card
                return fetch('/api/books')
                    .then(response => response.json())
                    .then(data => {
                        const books = data.books || [];
                        const book = books.find(b => b.cardUid === uid);
                        
                        if (book) {
                            console.log("Found book:", book);
                            // Book card found
                            sessionStorage.setItem('currentBook', JSON.stringify(book));
                            window.location.href = 'books.html';
                        } else {
                            console.log('Unregistered card');
                            alert('Unregistered card: ' + uid);
                        }
                    });
            }
        })
        .catch(error => {
            console.error('Error processing card:', error);
        });
}

// Start card scanning for registration
function startCardScan(elementId) {
    const element = document.getElementById(elementId);
    if (element) {
        element.textContent = 'Waiting for card scan...';
    }
    
    // Set mode on the ESP32
    fetch('/api/mode?mode=user')
        .then(response => response.text())
        .then(result => {
            console.log('Card scan mode set:', result);
            
            // Start checking for card
            checkForNewCard(elementId);
        })
        .catch(error => {
            console.error('Error setting scan mode:', error);
        });
}

// Start book card scanning
function startBookCardScan(elementId) {
    const element = document.getElementById(elementId);
    if (element) {
        element.textContent = 'Waiting for card scan...';
    }
    
    // Set mode on the ESP32
    fetch('/api/mode?mode=book')
        .then(response => response.text())
        .then(result => {
            console.log('Book card scan mode set:', result);
            
            // Start checking for card
            checkForNewCard(elementId);
        })
        .catch(error => {
            console.error('Error setting scan mode:', error);
        });
}

// New function to validate card UIDs
function validateCardUid(uid, elementId) {
    // First check against users
    return fetch('/api/users')
        .then(response => response.json())
        .then(userData => {
            const users = userData.users || [];
            
            // Check if card is already assigned to a user
            if (users.some(u => u.cardUid === uid)) {
                const element = document.getElementById(elementId);
                if (element) {
                    element.textContent = 'Card already registered to a user';
                }
                alert('This card is already registered to another user');
                return Promise.reject('Card already in use');
            }
            
            // Then check against books
            return fetch('/api/books');
        })
        .then(response => response.json())
        .then(bookData => {
            const books = bookData.books || [];
            
            // Check if card is already assigned to a book
            if (books.some(b => b.cardUid === uid)) {
                const element = document.getElementById(elementId);
                if (element) {
                    element.textContent = 'Card already registered to a book';
                }
                alert('This card is already registered to a book');
                return Promise.reject('Card already in use');
            }
            
            // If we get here, card is valid
            const element = document.getElementById(elementId);
            if (element) {
                element.textContent = uid;
            }
            // Clear the card UID after a successful read for registration
            fetch('/api/clear-card');
        })
        .catch(error => {
            if (error !== 'Card already in use') {
                console.error('Error validating card:', error);
                setTimeout(() => checkForNewCard(elementId, 0, 0), 1000);
            }
        });
}

// Modified checkForNewCard function to validate scanned cards
function checkForNewCard(elementId, attempts = 0, lastTimestamp = 0) {
    if (attempts > 15) {
        const element = document.getElementById(elementId);
        if (element) {
            element.textContent = 'Scan timeout. Try again.';
        }
        return;
    }
    
    fetch('/api/scan')
        .then(response => response.json())
        .then(data => {
            if (data && data.uid && data.uid !== "" && data.timestamp > lastTimestamp) {
                const scannedUid = data.uid;
                
                // Validate the card isn't already in use
                validateCardUid(scannedUid, elementId);
            } else {
                // Try again after a short delay
                setTimeout(() => checkForNewCard(elementId, attempts + 1, lastTimestamp), 1000);
            }
        })
        .catch(error => {
            console.error('Error checking for new card:', error);
            setTimeout(() => checkForNewCard(elementId, attempts + 1, lastTimestamp), 1000);
        });
}

// Check student login
function checkStudentLogin() {
    const currentUser = getCurrentUser();
    if (!currentUser || currentUser.type !== 'student') {
        window.location.href = 'index.html';
        return false;
    }
    
    const userInfo = document.getElementById('current-user');
    if (userInfo) {
        userInfo.textContent = currentUser.name || currentUser.studentId;
    }
    
    return true;
}

// Check admin login
function checkAdminLogin() {
    const currentUser = getCurrentUser();
    if (!currentUser || currentUser.type !== 'staff') {
        window.location.href = 'index.html';
        return false;
    }
    
    const userInfo = document.getElementById('current-user');
    if (userInfo) {
        userInfo.textContent = currentUser.username || currentUser.name;
    }
    
    return true;
}

// Admin Functions
// -----------------------------

// Load accounts
function loadAccounts() {
    fetch('/api/users')
        .then(response => response.json())
        .then(data => {
            const usersList = document.getElementById('accounts-list');
            if (!usersList) return;
            
            usersList.innerHTML = '';
            const users = data.users || [];
            
            users.forEach((user, index) => {
                const row = document.createElement('tr');
                row.innerHTML = `
                    <td>${user.username || user.studentId}</td>
                    <td>${user.type}</td>
                    <td>${user.name || '-'}</td>
                    <td>${user.cardUid || 'Not assigned'}</td>
                    <td>
                        <button class="btn-small delete-account-btn" data-index="${index}">Delete</button>
                    </td>
                `;
                usersList.appendChild(row);
            });
            
            // Add delete functionality
            document.querySelectorAll('.delete-account-btn').forEach(btn => {
                btn.addEventListener('click', function() {
                    const index = parseInt(this.getAttribute('data-index'));
                    deleteAccount(index);
                });
            });
        })
        .catch(error => {
            console.error('Error loading accounts:', error);
        });
}

// Delete account
function deleteAccount(index) {
    if (!confirm('Are you sure you want to delete this account?')) {
        return;
    }
    
    fetch('/api/users')
        .then(response => response.json())
        .then(data => {
            const users = data.users || [];
            
            // Remove the user at index
            users.splice(index, 1);
            
            // Save updated users
            return fetch('/api/users', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: `data=${encodeURIComponent(JSON.stringify({users: users}))}`,
            });
        })
        .then(response => {
            if (response.ok) {
                alert('Account deleted successfully');
                loadAccounts();
                
                // Return to first tab
                const accountsTab = document.querySelector('[data-tab="accounts"]');
                if (accountsTab) {
                    accountsTab.click();
                }
            } else {
                alert('Failed to delete account');
            }
        })
        .catch(error => {
            console.error('Error deleting account:', error);
            alert('Error deleting account');
        });
}

// Toggle account fields
function toggleAccountFields() {
    const accountType = document.getElementById('account-type').value;
    const studentFields = document.getElementById('student-fields');
    const staffFields = document.getElementById('staff-fields');
    
    if (accountType === 'student') {
        studentFields.classList.remove('hidden');
        staffFields.classList.add('hidden');
    } else {
        studentFields.classList.add('hidden');
        staffFields.classList.remove('hidden');
    }
}

// Create account - FIXED VERSION
function createAccount() {
    // 1. First, let's determine the account type
    const accountType = document.getElementById('account-type').value;
    console.log("Creating new account of type:", accountType);
    
    // 2. Build the user data object explicitly based on account type
    let userData = {
        type: accountType,
        cardUid: document.getElementById('card-uid-display').textContent
    };
    
    // Clear invalid card UIDs
    if (userData.cardUid === 'No card scanned' || userData.cardUid === 'Waiting for card scan...') {
        if (!confirm('No card has been scanned. Continue without a card?')) {
            return;
        }
        userData.cardUid = '';
    }
    
    // 3. Add type-specific properties
    if (accountType === 'student') {
        // Get all required fields for student accounts
        userData.studentId = document.getElementById('student-id').value.trim();
        userData.name = document.getElementById('student-name').value.trim();
        userData.email = document.getElementById('student-email').value.trim();
        userData.password = document.getElementById('student-password').value;
        
        // Simple validation
        if (!userData.studentId || !userData.password) {
            alert('Student ID and password are required');
            return;
        }
    } else { // staff
        // Get all required fields for staff accounts
        userData.username = document.getElementById('staff-username').value.trim();
        userData.password = document.getElementById('staff-password').value;
        
        // Simple validation
        if (!userData.username || !userData.password) {
            alert('Username and password are required');
            return;
        }
    }
    
    // 4. Log the data we're about to send (for debugging)
    console.log("New account data:", userData);
    
    // 5. Fetch existing users to check for duplicates
    fetch('/api/users')
        .then(response => {
            if (!response.ok) {
                throw new Error('Network response was not ok');
            }
            return response.json();
        })
        .then(data => {
            // Ensure users array exists
            const users = data.users || [];
            console.log("Retrieved existing users:", users.length);
            
            // Check for duplicates based on account type
            if (accountType === 'student') {
                const duplicateStudent = users.find(u => u.studentId === userData.studentId);
                if (duplicateStudent) {
                    alert('A student with this ID already exists');
                    return Promise.reject('Duplicate student ID');
                }
            } else { // staff
                const duplicateStaff = users.find(u => u.username === userData.username);
                if (duplicateStaff) {
                    alert('A staff member with this username already exists');
                    return Promise.reject('Duplicate username');
                }
            }
            
            // Check for duplicate card if one was provided
            if (userData.cardUid && userData.cardUid !== '') {
                const duplicateCard = users.find(u => u.cardUid === userData.cardUid);
                if (duplicateCard) {
                    alert('This card is already registered to another user');
                    return Promise.reject('Duplicate card');
                }
            }
            
            // 6. If we get here, we can add the new user
            users.push(userData);
            
            // 7. Format data for the API
            const updatedData = JSON.stringify({users: users});
            console.log("Sending updated users data...");
            
            // 8. Send the updated users data
            return fetch('/api/users', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: `data=${encodeURIComponent(updatedData)}`,
            });
        })
        .then(response => {
            if (!response.ok) {
                throw new Error('Failed to save user data: ' + response.status);
            }
            return response.text();
        })
        .then(result => {
            console.log("Account creation success:", result);
            alert('Account created successfully');
            
            // 9. Clear the form
            if (accountType === 'student') {
                document.getElementById('student-id').value = '';
                document.getElementById('student-name').value = '';
                document.getElementById('student-email').value = '';
                document.getElementById('student-password').value = '';
            } else {
                document.getElementById('staff-username').value = '';
                document.getElementById('staff-password').value = '';
            }
            document.getElementById('card-uid-display').textContent = 'No card scanned';
            
            // 10. Return to accounts tab and refresh the list
            const accountsTab = document.querySelector('[data-tab="accounts"]');
            if (accountsTab) {
                accountsTab.click();
            }
            loadAccounts();
        })
        .catch(error => {
            // 11. Handle expected rejection cases
            if (error === 'Duplicate student ID' || error === 'Duplicate username' || error === 'Duplicate card') {
                console.log("Validation error:", error);
                return; // Already showed an alert
            }
            
            // 12. Handle unexpected errors
            console.error("Account creation error:", error);
            alert('Error creating account: ' + error.message);
        });
}
// Load books
function loadBooks() {
    fetch('/api/books')
        .then(response => response.json())
        .then(data => {
            const booksList = document.getElementById('books-list');
            if (!booksList) return;
            
            booksList.innerHTML = '';
            const books = data.books || [];
            
            books.forEach((book, index) => {
                const row = document.createElement('tr');
                row.innerHTML = `
                    <td>${book.id}</td>
                    <td>${book.title}</td>
                    <td>${book.author}</td>
                    <td>${book.borrowed ? '<span class="status status-borrowed">Borrowed</span>' : '<span class="status status-available">Available</span>'}</td>
                    <td>${book.borrowed ? '-' : book.shelf + ' Floor ' + book.floor}</td>
                    <td>${book.borrowedBy || '-'}</td>
                    <td>${book.returnDate ? new Date(book.returnDate).toLocaleDateString() : '-'}</td>
                    <td>
                        <button class="btn-small delete-book-btn" data-index="${index}">Delete</button>
                    </td>
                `;
                booksList.appendChild(row);
            });
            
            // Add delete functionality
            document.querySelectorAll('.delete-book-btn').forEach(btn => {
                btn.addEventListener('click', function() {
                    const index = parseInt(this.getAttribute('data-index'));
                    deleteBook(index);
                });
            });
        })
        .catch(error => {
            console.error('Error loading books:', error);
        });
}

// Delete book function - updated to prevent deletion of borrowed books
function deleteBook(index) {
    fetch('/api/books')
        .then(response => response.json())
        .then(data => {
            const books = data.books || [];
            const book = books[index];
            
            // Check if book is borrowed
            if (book && book.borrowed) {
                alert('This book cannot be deleted because it is currently borrowed.');
                return Promise.reject('Book is borrowed');
            }
            
            // Confirm deletion if the book is not borrowed
            if (!confirm('Are you sure you want to delete this book?')) {
                return Promise.reject('Deletion cancelled');
            }
            
            // Remove the book at index
            books.splice(index, 1);
            
            // Save updated books
            return fetch('/api/books', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: `data=${encodeURIComponent(JSON.stringify({books: books}))}`,
            });
        })
        .then(response => {
            if (response && response.ok) {
                alert('Book deleted successfully');
                
                // Return to books tab
                const booksTab = document.querySelector('[data-tab="books"]');
                if (booksTab) {
                    booksTab.click();
                }
                loadBooks();
            } else {
                alert('Failed to delete book');
            }
        })
        .catch(error => {
            if (error !== 'Book is borrowed' && error !== 'Deletion cancelled') {
                console.error('Error deleting book:', error);
                alert('Error deleting book');
            }
        });
}

// Add book - FIXED VERSION
function addBook() {
    const bookData = {
        id: document.getElementById('book-id').value,
        isbn: document.getElementById('book-id').value,
        title: document.getElementById('book-title').value,
        author: document.getElementById('book-author').value,
        shelf: document.getElementById('book-shelf').value,
        floor: document.getElementById('book-floor').value,
        borrowed: false,
        cardUid: document.getElementById('book-card-uid').textContent,
        history: []
    };
    
    if (!bookData.id || !bookData.title || !bookData.author || !bookData.shelf || !bookData.floor) {
        alert('Book ID, title, author, shelf, and floor are required');
        return;
    }
    
    if (bookData.cardUid === 'No card scanned' || bookData.cardUid === 'Waiting for card scan...') {
        if (!confirm('No card has been scanned. Continue without a card?')) {
            return;
        }
        bookData.cardUid = '';
    }
    
    // First check user database to ensure the card isn't already assigned to a user
    fetch('/api/users')
        .then(response => {
            if (!response.ok) {
                throw new Error('Failed to fetch user data');
            }
            return response.json();
        })
        .then(userData => {
            const users = userData.users || [];
            
            // Check if card is already assigned to a user
            if (bookData.cardUid && bookData.cardUid !== '') {
                if (users.some(u => u.cardUid === bookData.cardUid)) {
                    alert('This card is already registered to a user');
                    return Promise.reject('Card already registered to user');
                }
            }
            
            // Now check book database
            return fetch('/api/books');
        })
        .then(response => {
            if (!response.ok) {
                throw new Error('Failed to fetch book data');
            }
            return response.json();
        })
        .then(data => {
            const books = data.books || [];
            
            // Check for duplicate book ID
            if (books.some(b => b.id === bookData.id)) {
                alert('A book with this ID already exists');
                return Promise.reject('Duplicate ID');
            }
            
            // Check for duplicate card UID if one was provided
            if (bookData.cardUid && bookData.cardUid !== '') {
                if (books.some(b => b.cardUid === bookData.cardUid)) {
                    alert('This card is already registered to another book');
                    return Promise.reject('Duplicate card');
                }
            }
            
            // Add the new book
            books.push(bookData);
            
            // Save updated books
            return fetch('/api/books', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: `data=${encodeURIComponent(JSON.stringify({books: books}))}`,
            });
        })
        .then(response => {
            if (!response.ok) {
                throw new Error('Failed to save book data');
            }
            return response.text();
        })
        .then(result => {
            alert('Book added successfully');
            
            // Clear the form
            document.getElementById('book-id').value = '';
            document.getElementById('book-title').value = '';
            document.getElementById('book-author').value = '';
            document.getElementById('book-shelf').value = '';
            document.getElementById('book-floor').value = '';
            document.getElementById('book-card-uid').textContent = 'No card scanned';
            
            // Return to books tab
            const booksTab = document.querySelector('[data-tab="books"]');
            if (booksTab) {
                booksTab.click();
            }
            loadBooks();
        })
        .catch(error => {
            if (error !== 'Duplicate ID' && error !== 'Duplicate card' && error !== 'Card already registered to user') {
                console.error('Error adding book:', error);
                alert('Error adding book: ' + error.message);
            }
        });
}

// Student Functions
// -----------------------------

// Load borrowed books
function loadBorrowedBooks() {
    const currentUser = getCurrentUser();
    if (!currentUser) return;
    
    fetch('/api/books')
        .then(response => response.json())
        .then(data => {
            const borrowedList = document.getElementById('borrowed-books-list');
            if (!borrowedList) return;
            
            borrowedList.innerHTML = '';
            const books = data.books || [];
            const userId = currentUser.studentId || currentUser.username;
            
            const now = new Date();
            
            books.forEach(book => {
                if (book.borrowed && book.borrowedBy === userId) {
                    const returnDate = new Date(book.returnDate);
                    const daysLeft = Math.ceil((returnDate - now) / (1000 * 60 * 60 * 24));
                    
                    // Calculate penalty
                    let penalty = 0;
                    if (daysLeft < 0) {
                        penalty = Math.abs(daysLeft);
                    }
                    
                    const row = document.createElement('tr');
                    row.innerHTML = `
                        <td>${book.id}</td>
                        <td>${book.title}</td>
                        <td>${new Date(book.borrowDate).toLocaleDateString()}</td>
                        <td>${new Date(book.returnDate).toLocaleDateString()}</td>
                        <td>${daysLeft > 0 ? daysLeft : '<span class="status status-overdue">Overdue</span>'}</td>
                        <td>${penalty > 0 ? '<span class="text-danger">' + penalty + '</span>' : '0'}</td>
                    `;
                    borrowedList.appendChild(row);
                }
            });
        })
        .catch(error => {
            console.error('Error loading borrowed books:', error);
        });
}

// Load student profile
function loadStudentProfile() {
    const currentUser = getCurrentUser();
    if (!currentUser) return;
    
    const profileInfo = document.getElementById('student-info');
    if (profileInfo) {
        profileInfo.innerHTML = `
            <div><label>Student ID:</label> ${currentUser.studentId}</div>
            <div><label>Name:</label> ${currentUser.name || '-'}</div>
            <div><label>Email:</label> ${currentUser.email || '-'}</div>
            <div><label>Card UID:</label> ${currentUser.cardUid || 'Not assigned'}</div>
        `;
    }
}

// Load borrowing history
function loadBorrowingHistory() {
    const currentUser = getCurrentUser();
    if (!currentUser) return;
    
    fetch('/api/books')
        .then(response => response.json())
        .then(data => {
            const historyList = document.getElementById('history-list');
            if (!historyList) return;
            
            historyList.innerHTML = '';
            const books = data.books || [];
            const userId = currentUser.studentId || currentUser.username;
            
            // Get 6 months ago date
            const sixMonthsAgo = new Date();
            sixMonthsAgo.setMonth(sixMonthsAgo.getMonth() - 6);
            
            // Collect history
            const history = [];
            
            books.forEach(book => {
                if (book.history && book.history.length > 0) {
                    book.history.forEach(entry => {
                        if (entry.username === userId) {
                            const borrowDate = new Date(entry.borrowDate);
                            if (borrowDate >= sixMonthsAgo) {
                                history.push({
                                    id: book.id,
                                    title: book.title,
                                    borrowDate: borrowDate,
                                    returnDate: entry.returnDate ? new Date(entry.returnDate) : null,
                                    status: entry.returnDate ? 'Returned' : 'Borrowed'
                                });
                            }
                        }
                    });
                }
            });
            
            // Sort by borrow date (newest first)
            history.sort((a, b) => b.borrowDate - a.borrowDate);
            
            // Display history
            history.forEach(item => {
                const row = document.createElement('tr');
                row.innerHTML = `
                    <td>${item.id}</td>
                    <td>${item.title}</td>
                    <td>${item.borrowDate.toLocaleDateString()}</td>
                    <td>${item.returnDate ? item.returnDate.toLocaleDateString() : '-'}</td>
                    <td>${item.status === 'Borrowed' ? '<span class="status status-borrowed">Borrowed</span>' : 'Returned'}</td>
                `;
                historyList.appendChild(row);
            });
        })
        .catch(error => {
            console.error('Error loading history:', error);
        });
}

// Book Functions
// -----------------------------

// Load book details
function loadBookDetails() {
    const bookJson = sessionStorage.getItem('currentBook');
    if (!bookJson) {
        const bookNotFound = document.getElementById('book-not-found');
        const bookDetails = document.getElementById('book-details');
        
        if (bookNotFound) bookNotFound.classList.remove('hidden');
        if (bookDetails) bookDetails.classList.add('hidden');
        return;
    }
    
    const book = JSON.parse(bookJson);
    
    // Set basic details
    document.getElementById('book-title').textContent = book.title;
    document.getElementById('book-id').textContent = book.id;
    document.getElementById('book-author').textContent = book.author;
    
    // Set status with appropriate class
    const statusElement = document.getElementById('book-status');
    if (statusElement) {
        if (book.borrowed) {
            statusElement.textContent = 'Borrowed';
            statusElement.className = 'status status-borrowed';
        } else {
            statusElement.textContent = 'Available';
            statusElement.className = 'status status-available';
        }
    }
    
    document.getElementById('book-card-uid').textContent = book.cardUid || 'Not available';
    
    // Handle location display with new format
    if (book.shelf && book.floor) {
        document.getElementById('book-location').textContent = `${book.shelf} Floor ${book.floor}`;
    } else if (book.location) {
        // Backward compatibility
        document.getElementById('book-location').textContent = book.location;
    } else {
        document.getElementById('book-location').textContent = 'Not specified';
    }
    
    // Show/hide sections based on status
    if (book.borrowed) {
        // Book is borrowed
        document.getElementById('borrowed-info').classList.remove('hidden');
        document.getElementById('return-info').classList.remove('hidden');
        document.getElementById('days-info').classList.remove('hidden');
        document.getElementById('borrowed-by').textContent = book.borrowedBy;
        document.getElementById('return-date').textContent = new Date(book.returnDate).toLocaleDateString();
        
        // Calculate days until return
        const returnDate = new Date(book.returnDate);
        const now = new Date();
        const daysLeft = Math.ceil((returnDate - now) / (1000 * 60 * 60 * 24));
        
        const daysLeftElement = document.getElementById('days-left');
        if (daysLeft > 0) {
            daysLeftElement.textContent = daysLeft;
            daysLeftElement.className = 'text-success';
        } else {
            daysLeftElement.textContent = 'Overdue';
            daysLeftElement.className = 'text-danger';
        }
        
        // Calculate and display penalty if overdue
        if (daysLeft < 0) {
            const penalty = Math.abs(daysLeft);
            document.getElementById('penalty-info').classList.remove('hidden');
            document.getElementById('penalty-amount').textContent = `${penalty} INR`;
        } else {
            document.getElementById('penalty-info').classList.add('hidden');
        }
        
        // Show buttons based on user
        const currentUser = getCurrentUser();
        if (currentUser && (currentUser.studentId === book.borrowedBy || currentUser.username === book.borrowedBy)) {
            document.getElementById('borrow-btn').classList.add('hidden');
            document.getElementById('return-btn').classList.remove('hidden');
        } else {
            document.getElementById('borrow-btn').classList.add('hidden');
            document.getElementById('return-btn').classList.add('hidden');
        }
    } else {
        // Book is available
        document.getElementById('borrowed-info').classList.add('hidden');
        document.getElementById('return-info').classList.add('hidden');
        document.getElementById('days-info').classList.add('hidden');
        document.getElementById('penalty-info').classList.add('hidden');
        
        // Show borrow button for students
        const currentUser = getCurrentUser();
        if (currentUser && currentUser.type === 'student') {
            document.getElementById('borrow-btn').classList.remove('hidden');
            document.getElementById('return-btn').classList.add('hidden');
        } else {
            document.getElementById('borrow-btn').classList.add('hidden');
            document.getElementById('return-btn').classList.add('hidden');
        }
    }
}

// Borrow book
function borrowBook() {
    const currentUser = getCurrentUser();
    if (!currentUser || currentUser.type !== 'student') {
        alert('You must be logged in as a student to borrow books');
        return;
    }
    
    const bookJson = sessionStorage.getItem('currentBook');
    if (!bookJson) return;
    
    const book = JSON.parse(bookJson);
    if (book.borrowed) {
        alert('This book is already borrowed');
        return;
    }
    
    fetch('/api/books')
        .then(response => response.json())
        .then(data => {
            const books = data.books || [];
            const bookIndex = books.findIndex(b => b.id === book.id);
            
            if (bookIndex === -1) {
                alert('Book not found');
                return Promise.reject('Book not found');
            }
            
            // Check again if book is borrowed (it might have been borrowed by someone else)
            if (books[bookIndex].borrowed) {
                alert('This book has already been borrowed by someone else');
                return Promise.reject('Book already borrowed');
            }
            
            // Set borrow values
            const now = new Date();
            const returnDate = new Date();
            returnDate.setDate(now.getDate() + 14); // 14 days loan
            
            books[bookIndex].borrowed = true;
            books[bookIndex].borrowedBy = currentUser.studentId;
            books[bookIndex].borrowDate = now.toISOString();
            books[bookIndex].returnDate = returnDate.toISOString();
            
            // Add to history
            if (!books[bookIndex].history) {
                books[bookIndex].history = [];
            }
            
            books[bookIndex].history.push({
                username: currentUser.studentId,
                borrowDate: now.toISOString(),
                returnDate: null
            });
            
            // Update sessionStorage
            sessionStorage.setItem('currentBook', JSON.stringify(books[bookIndex]));
            
            // Save to server
            return fetch('/api/books', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: `data=${encodeURIComponent(JSON.stringify({books: books}))}`,
            });
        })
        .then(response => {
            if (response && response.ok) {
                alert('Book borrowed successfully. You have 14 days to return it.');
                loadBookDetails(); // Refresh the page
            } else {
                alert('Failed to borrow book');
            }
        })
        .catch(error => {
            if (error !== 'Book not found' && error !== 'Book already borrowed') {
                console.error('Error borrowing book:', error);
                alert('Error borrowing book');
            }
        });
}

// Return book
function returnBook() {
    const currentUser = getCurrentUser();
    if (!currentUser) {
        alert('You must be logged in to return books');
        return;
    }
    
    const bookJson = sessionStorage.getItem('currentBook');
    if (!bookJson) return;
    
    const book = JSON.parse(bookJson);
    
    if (!book.borrowed) {
        alert('This book is not currently borrowed');
        return;
    }
    
    if (book.borrowedBy !== currentUser.studentId && book.borrowedBy !== currentUser.username) {
        alert('You can only return books that you have borrowed');
        return;
    }
    
    fetch('/api/books')
        .then(response => response.json())
        .then(data => {
            const books = data.books || [];
            const bookIndex = books.findIndex(b => b.id === book.id);
            
            if (bookIndex === -1) {
                alert('Book not found');
                return Promise.reject('Book not found');
            }
            
            // Update book status
            books[bookIndex].borrowed = false;
            books[bookIndex].borrowedBy = null;
            books[bookIndex].borrowDate = null;
            books[bookIndex].returnDate = null;
            
            // Update history
            const now = new Date();
            
            if (books[bookIndex].history && books[bookIndex].history.length > 0) {
                // Find the latest borrow record for this user
                for (let i = books[bookIndex].history.length - 1; i >= 0; i--) {
                    const record = books[bookIndex].history[i];
                    if ((record.username === currentUser.studentId || record.username === currentUser.username) && !record.returnDate) {
                        record.returnDate = now.toISOString();
                        break;
                    }
                }
            }
            
            // Update sessionStorage
            sessionStorage.setItem('currentBook', JSON.stringify(books[bookIndex]));
            
            // Save to server
            return fetch('/api/books', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: `data=${encodeURIComponent(JSON.stringify({books: books}))}`,
            });
        })
        .then(response => {
            if (response && response.ok) {
                alert('Book returned successfully');
                // After returning, redirect to index page
                window.location.href = 'index.html';
            } else {
                alert('Failed to return book');
            }
        })
        .catch(error => {
            if (error !== 'Book not found') {
                console.error('Error returning book:', error);
                alert('Error returning book');
            }
        });
}

// Return book from index page
function returnBookFromIndex() {
    console.log("Return book function activated");
    
    // Set flag to prevent default card processing
    inReturnMode = true;
    
    // Display the return book section
    const returnSection = document.getElementById('return-book-section');
    if (returnSection) {
        returnSection.classList.remove('hidden');
    }
    
    // Start checking for card scans
    const status = document.getElementById('return-status');
    if (status) {
        status.textContent = 'Please scan the book card to return...';
    }
    
    // Set up a polling interval for card scans
    const returnBookInterval = setInterval(function() {
        fetch('/api/scan')
            .then(response => response.json())
            .then(data => {
                if (data && data.uid && data.uid !== "") {
                    // Clear the interval to stop polling
                    clearInterval(returnBookInterval);
                    
                    // Process the book return with the scanned card
                    processBookReturn(data.uid);
                }
            })
            .catch(error => {
                console.error('Error checking for book card:', error);
                if (status) {
                    status.textContent = 'Error checking card. Please try again.';
                }
            });
    }, 2000); // Check every 2 seconds
    
    // Set up cancel button
    const cancelBtn = document.createElement('button');
    cancelBtn.textContent = 'Cancel';
    cancelBtn.className = 'btn';
    cancelBtn.style.marginTop = '10px';
    cancelBtn.addEventListener('click', function() {
        clearInterval(returnBookInterval);
        inReturnMode = false;
        if (returnSection) {
            returnSection.classList.add('hidden');
        }
    });
    
    // Add cancel button to return section
    if (returnSection && !returnSection.querySelector('button[cancel-return]')) {
        cancelBtn.setAttribute('cancel-return', 'true');
        returnSection.appendChild(cancelBtn);
    }
}

// Process book return
function processBookReturn(uid) {
    console.log("Processing book return for card:", uid);
    const status = document.getElementById('return-status');
    
    // Check if it's a book card
    fetch('/api/books')
        .then(response => response.json())
        .then(data => {
            const books = data.books || [];
            const book = books.find(b => b.cardUid === uid);
            
            if (book) {
                if (book.borrowed) {
                    // Book is borrowed, proceed with return
                    if (status) {
                        status.textContent = `Found book: ${book.title}. Processing return...`;
                    }
                    
                    const bookIndex = books.indexOf(book);
                    
                    // Update book status
                    books[bookIndex].borrowed = false;
                    books[bookIndex].borrowedBy = null;
                    books[bookIndex].borrowDate = null;
                    books[bookIndex].returnDate = null;
                    
                    // Update history
                    const now = new Date();
                    
                    if (books[bookIndex].history && books[bookIndex].history.length > 0) {
                        // Find the latest borrow record
                        for (let i = books[bookIndex].history.length - 1; i >= 0; i--) {
                            const record = books[bookIndex].history[i];
                            if (!record.returnDate) {
                                record.returnDate = now.toISOString();
                                break;
                            }
                        }
                    }
                    
                    // Save to server
                    return fetch('/api/books', {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/x-www-form-urlencoded',
                        },
                        body: `data=${encodeURIComponent(JSON.stringify({books: books}))}`,
                    })
                    .then(response => {
                        if (response.ok) {
                            if (status) {
                                status.textContent = `Book "${book.title}" returned successfully.`;
                                
                                // Add a message about successful return with green background
                                const successMsg = document.createElement('div');
                                successMsg.textContent = `Book "${book.title}" returned successfully.`;
                                successMsg.style.backgroundColor = '#dff0d8';
                                successMsg.style.color = '#3c763d';
                                successMsg.style.padding = '10px';
                                successMsg.style.borderRadius = '4px';
                                successMsg.style.marginTop = '15px';
                                
                                const returnSection = document.getElementById('return-book-section');
                                if (returnSection) {
                                    // Remove any existing success messages
                                    const existingMsg = returnSection.querySelector('.success-msg');
                                    if (existingMsg) {
                                        returnSection.removeChild(existingMsg);
                                    }
                                    
                                    successMsg.className = 'success-msg';
                                    returnSection.appendChild(successMsg);
                                }
                            }
                            
                            // Reset return mode after successful return
                            setTimeout(function() {
                                inReturnMode = false;
                                // Don't hide the return section so user can see the success message
                            }, 5000);
                            
                            return true;
                        } else {
                            throw new Error('Failed to save book data');
                        }
                    });
                } else {
                    if (status) {
                        status.textContent = 'This book is not currently borrowed.';
                    }
                    
                    // Reset after 5 seconds
                    setTimeout(function() {
                        inReturnMode = false;
                        returnBookFromIndex();
                    }, 5000);
                    
                    return false;
                }
            } else {
                if (status) {
                    status.textContent = 'This card is not registered to any book.';
                }
                
                // Reset after 5 seconds
                setTimeout(function() {
                    inReturnMode = false;
                    returnBookFromIndex();
                }, 5000);
                
                return false;
            }
        })
        .catch(error => {
            console.error('Error processing book return:', error);
            if (status) {
                status.textContent = 'Error processing return. Please try again.';
            }
            
            // Reset return mode after error
            setTimeout(function() {
                inReturnMode = false;
                returnBookFromIndex();
            }, 5000);
        });
}

// Process book card for borrowing
function processBookCardForBorrow(uid) {
    console.log("Processing book card for borrow:", uid);
    fetch('/api/books')
        .then(response => response.json())
        .then(data => {
            const books = data.books || [];
            const book = books.find(b => b.cardUid === uid);
            
            const status = document.getElementById('borrow-status');
            
            if (book) {
                console.log("Found book for borrow:", book);
                if (status) {
                    status.textContent = `Found book: ${book.title}`;
                }
                
                if (book.borrowed) {
                    if (status) {
                        status.textContent = `Book "${book.title}" is already borrowed and unavailable.`;
                    }
                    alert(`Book "${book.title}" is already borrowed and unavailable.`);
                } else {
                    // Borrow the book
                    const currentUser = getCurrentUser();
                    if (currentUser) {
                        // Set borrow values
                        const now = new Date();
                        const returnDate = new Date();
                        returnDate.setDate(now.getDate() + 14); // 14 days loan
                        
                        const bookIndex = books.indexOf(book);
                        books[bookIndex].borrowed = true;
                        books[bookIndex].borrowedBy = currentUser.studentId;
                        books[bookIndex].borrowDate = now.toISOString();
                        books[bookIndex].returnDate = returnDate.toISOString();
                        
                        // Add to history
                        if (!books[bookIndex].history) {
                            books[bookIndex].history = [];
                        }
                        
                        books[bookIndex].history.push({
                            username: currentUser.studentId,
                            borrowDate: now.toISOString(),
                            returnDate: null
                        });
                        
                        // Save to server
                        fetch('/api/books', {
                            method: 'POST',
                            headers: {
                                'Content-Type': 'application/x-www-form-urlencoded',
                            },
                            body: `data=${encodeURIComponent(JSON.stringify({books: books}))}`,
                        })
                        .then(response => {
                            if (response.ok) {
                                if (status) {
                                    status.textContent = `Book "${book.title}" borrowed successfully. You have 14 days to return it.`;
                                }
                                alert(`Book "${book.title}" borrowed successfully. You have 14 days to return it.`);
                                loadBorrowedBooks(); // Refresh the borrowed books list
                            } else {
                                if (status) {
                                    status.textContent = 'Failed to borrow book';
                                }
                                alert('Failed to borrow book');
                            }
                        })
                        .catch(error => {
                            console.error('Error saving borrowed book:', error);
                            if (status) {
                                status.textContent = 'Error borrowing book';
                            }
                            alert('Error borrowing book');
                        });
                    }
                }
            } else {
                console.log("Book not found for card:", uid);
                if (status) {
                    status.textContent = 'This card is not registered to any book.';
                }
                alert('This card is not registered to any book.');
            }
        })
        .catch(error => {
            console.error('Error processing book card:', error);
            const status = document.getElementById('borrow-status');
            if (status) {
                status.textContent = 'Error processing card';
            }
        });
}

// Page Initialization
// -----------------------------

// Add console logging for debugging
console.log("Script loaded and running");

document.addEventListener('DOMContentLoaded', function() {
    console.log("DOM fully loaded");
    
    // Common elements
    const logoutBtn = document.getElementById('logout-btn');
    if (logoutBtn) {
        logoutBtn.addEventListener('click', logout);
        console.log("Logout button initialized");
    }
    
    // Setup tabs if present
    if (document.querySelector('.tabs')) {
        setupTabs();
        console.log("Tabs initialized");
    }
    
    // Determine which page we're on
    const path = window.location.pathname;
    console.log("Current path:", path);
    
    // Index page
    if (path.endsWith('index.html') || path === '/' || path.endsWith('/')) {
        console.log("Index page detected");
        // Setup login forms
        const staffForm = document.getElementById('staff-login-form');
        const studentForm = document.getElementById('student-login-form');
        const returnBookBtn = document.getElementById('return-book-btn');
        
        if (staffForm) {
            staffForm.addEventListener('submit', function(e) {
                e.preventDefault();
                const username = document.getElementById('staff-username').value;
                const password = document.getElementById('staff-password').value;
                loginUser(username, password, 'staff');
            });
            console.log("Staff login form initialized");
        }
        
        if (studentForm) {
            studentForm.addEventListener('submit', function(e) {
                e.preventDefault();
                const studentId = document.getElementById('student-id').value;
                const password = document.getElementById('student-password').value;
                loginUser(studentId, password, 'student');
            });
            console.log("Student login form initialized");
        }
        
        if (returnBookBtn) {
            returnBookBtn.addEventListener('click', returnBookFromIndex);
            console.log("Return book button initialized");
        }
        
        // Start polling for card scans
        pollForCardScan();
        console.log("Card polling started");
    }
    
    // Admin page
    else if (path.includes('admin.html')) {
        console.log("Admin page detected");
        if (checkAdminLogin()) {
            // Load data
            loadAccounts();
            loadBooks();
            
            // Setup account type toggling
            const accountType = document.getElementById('account-type');
            if (accountType) {
                accountType.addEventListener('change', toggleAccountFields);
                toggleAccountFields(); // Initial setup
                console.log("Account type toggling initialized");
            }
            
            // Setup card scanning
            const scanCardBtn = document.getElementById('scan-card-btn');
            if (scanCardBtn) {
                scanCardBtn.addEventListener('click', function() {
                    startCardScan('card-uid-display');
                });
                console.log("User card scanning initialized");
            }
            
            const scanBookCardBtn = document.getElementById('scan-book-card');
            if (scanBookCardBtn) {
                scanBookCardBtn.addEventListener('click', function() {
                    startBookCardScan('book-card-uid');
                });
                console.log("Book card scanning initialized");
            }
            
            // Setup form submissions
            const createAccountForm = document.getElementById('create-account-form');
            if (createAccountForm) {
                createAccountForm.addEventListener('submit', function(e) {
                    e.preventDefault();
                    createAccount();
                });
                console.log("Create account form initialized");
            }
            
            const addBookForm = document.getElementById('add-book-form');
            if (addBookForm) {
                addBookForm.addEventListener('submit', function(e) {
                    e.preventDefault();
                    addBook();
                });
                console.log("Add book form initialized");
            }
        }
    }
    
    // Student page
    else if (path.includes('student.html')) {
        console.log("Student page detected");
        if (checkStudentLogin()) {
            // Load student data
            loadBorrowedBooks();
            loadStudentProfile();
            loadBorrowingHistory();
            console.log("Student data loaded");
            
            // Setup check borrow
            const checkBorrowBtn = document.getElementById('check-borrow-card');
            if (checkBorrowBtn) {
                checkBorrowBtn.addEventListener('click', function() {
                    const status = document.getElementById('borrow-status');
                    if (status) {
                        status.textContent = 'Checking for scanned card...';
                    }
                    
                    fetch('/api/scan')
                        .then(response => response.json())
                        .then(data => {
                            if (data && data.uid && data.uid !== "") {
                                // Check if it's a book card
                                processBookCardForBorrow(data.uid);
                            } else {
                                if (status) {
                                    status.textContent = 'No card detected. Please scan a book card.';
                                }
                            }
                        })
                        .catch(error => {
                            console.error('Error checking card:', error);
                            if (status) {
                                status.textContent = 'Error checking card.';
                            }
                        });
                });
                console.log("Borrow card button initialized");
            }
        }
    }
    
    // Book details page
    else if (path.includes('books.html')) {
        console.log("Book details page detected");
        loadBookDetails();
        
        // Setup buttons
        const backBtn = document.getElementById('back-btn');
        if (backBtn) {
            backBtn.addEventListener('click', function() {
                window.history.back();
            });
            console.log("Back button initialized");
        }
        
        const homeBtn = document.getElementById('home-btn');
        if (homeBtn) {
            homeBtn.addEventListener('click', function() {
                window.location.href = 'index.html';
            });
            console.log("Home button initialized");
        }
        
        const borrowBtn = document.getElementById('borrow-btn');
        if (borrowBtn) {
            borrowBtn.addEventListener('click', borrowBook);
            console.log("Borrow button initialized");
        }
        
        const returnBtn = document.getElementById('return-btn');
        if (returnBtn) {
            returnBtn.addEventListener('click', returnBook);
            console.log("Return button initialized");
        }
    }
});